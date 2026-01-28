#include <stdint.h>
#include <stddef.h>

#include "fat16.h"
#include "vfs.h"
#include "ata.h"
#include "bcache.h"
#include "serial.h"

#define ATTR_READONLY 0x01
#define ATTR_HIDDEN   0x02
#define ATTR_VOLUME   0x08
#define ATTR_DIR      0x10
#define ATTR_LFN      0x0F

#define CHAIN_END     0xFFF8    /* >= this means last cluster */
#define DIRENT_SIZE   32
#define FAT_PATH_MAX  64

/* Everything in sectors, absolute on the disk. */
static int      mounted;
static int      registered;         /* vfs_mount happened (no unmount exists) */
static uint32_t part_start;
static uint32_t fat_start;
static uint32_t root_start;
static uint32_t root_sectors;
static uint32_t data_start;
static uint32_t sectors_per_cluster;
static uint32_t root_entries;
static uint32_t num_fats;
static uint32_t sectors_per_fat;
static uint32_t total_clusters;

struct dirent_raw {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  reserved[8];
    uint16_t cluster_high;      /* FAT32 only, zero here */
    uint16_t time, date;
    uint16_t first_cluster;
    uint32_t size;
} __attribute__((packed));

static uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32(const uint8_t* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

static uint16_t fat_entry(uint16_t cluster) {
    uint32_t byte_off = (uint32_t)cluster * 2;
    struct buf* b = bread(fat_start + byte_off / 512);
    if (!b) return 0xFFFF;

    return read_u16(&b->data[byte_off % 512]);
}

/* Writes go to every FAT copy - the second FAT exists so recovery tools
 * have something to disagree with, and leaving it stale defeats that. */
static int fat_set_entry(uint16_t cluster, uint16_t value) {
    uint32_t byte_off = (uint32_t)cluster * 2;

    for (uint32_t f = 0; f < num_fats; ++f) {
        struct buf* b = bread(fat_start + f * sectors_per_fat + byte_off / 512);
        if (!b) return -1;

        b->data[byte_off % 512]     = (uint8_t)(value & 0xFF);
        b->data[byte_off % 512 + 1] = (uint8_t)(value >> 8);
        bwrite(b);
    }
    return 0;
}

/* First free cluster, marked end-of-chain. 0 means the disk is full. */
static uint16_t alloc_cluster(void) {
    for (uint32_t cl = 2; cl < total_clusters + 2; ++cl) {
        if (fat_entry((uint16_t)cl) == 0x0000) {
            if (fat_set_entry((uint16_t)cl, 0xFFFF) != 0) return 0;
            return (uint16_t)cl;
        }
    }
    return 0;
}

static void free_chain(uint16_t cluster) {
    while (cluster >= 2 && cluster < CHAIN_END) {
        uint16_t next = fat_entry(cluster);
        fat_set_entry(cluster, 0x0000);
        cluster = next;
    }
}

static uint32_t cluster_to_sector(uint16_t cluster) {
    return data_start + (uint32_t)(cluster - 2) * sectors_per_cluster;
}

/* "hello.txt" (any case) -> "HELLO   TXT". Non-zero if it doesn't fit 8.3. */
static int to_83(const char* comp, uint32_t len, uint8_t out[11]) {
    for (int i = 0; i < 11; ++i) out[i] = ' ';

    uint32_t dot = len;
    for (uint32_t i = 0; i < len; ++i) {
        if (comp[i] == '.') { dot = i; break; }
    }

    uint32_t base = dot, ext = (dot < len) ? len - dot - 1 : 0;
    if (base == 0 || base > 8 || ext > 3) return -1;

    for (uint32_t i = 0; i < base; ++i) {
        char c = comp[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[i] = (uint8_t)c;
    }
    for (uint32_t i = 0; i < ext; ++i) {
        char c = comp[dot + 1 + i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[8 + i] = (uint8_t)c;
    }

    return 0;
}

/* "HELLO   TXT" -> "HELLO.TXT" for listings. */
static void from_83(const uint8_t name[11], char out[13]) {
    int n = 0;
    for (int i = 0; i < 8 && name[i] != ' '; ++i) out[n++] = (char)name[i];

    if (name[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && name[i] != ' '; ++i) out[n++] = (char)name[i];
    }
    out[n] = '\0';
}

/* Walk a directory - the fixed root area when cluster is 0, a cluster chain
 * otherwise - handing each entry to cb along with where it lives on disk,
 * so a caller can rewrite it in place. Deleted and end-marker entries are
 * passed too (write paths hunt for free slots); cb filters what it wants.
 * cb returns non-zero to stop, and a 0x00 first byte still ends the walk
 * after the callback has seen it. */
typedef int (*dirent_cb)(const struct dirent_raw* e, uint32_t sector,
                         uint32_t index, void* ctx);

static int iterate_dir(uint16_t cluster, dirent_cb cb, void* ctx) {
    if (cluster == 0) {
        for (uint32_t s = 0; s < root_sectors; ++s) {
            struct buf* b = bread(root_start + s);
            if (!b) return -1;

            for (uint32_t i = 0; i < 512 / DIRENT_SIZE; ++i) {
                const struct dirent_raw* e =
                    (const struct dirent_raw*)&b->data[i * DIRENT_SIZE];

                int end = (e->name[0] == 0x00);
                if (cb(e, root_start + s, i, ctx)) return 0;
                if (end) return 0;
            }
        }
        return 0;
    }

    while (cluster < CHAIN_END && cluster >= 2) {
        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            uint32_t sector = cluster_to_sector(cluster) + s;
            struct buf* b = bread(sector);
            if (!b) return -1;

            for (uint32_t i = 0; i < 512 / DIRENT_SIZE; ++i) {
                const struct dirent_raw* e =
                    (const struct dirent_raw*)&b->data[i * DIRENT_SIZE];

                int end = (e->name[0] == 0x00);
                if (cb(e, sector, i, ctx)) return 0;
                if (end) return 0;
            }
        }
        cluster = fat_entry(cluster);
    }

    return 0;
}

/* Live entries only: the filter most callbacks want. */
static int is_live(const struct dirent_raw* e) {
    if (e->name[0] == 0x00 || e->name[0] == 0xE5) return 0;
    if (e->attr == ATTR_LFN) return 0;
    if (e->attr & ATTR_VOLUME) return 0;
    return 1;
}

struct search {
    uint8_t want[11];
    int     found;
    struct dirent_raw entry;
    uint32_t sector;        /* where the entry lives, for rewriting it */
    uint32_t index;
};

static int search_cb(const struct dirent_raw* e, uint32_t sector,
                     uint32_t index, void* ctx) {
    struct search* s = (struct search*)ctx;

    if (!is_live(e)) return 0;

    for (int i = 0; i < 11; ++i) {
        if (e->name[i] != s->want[i]) return 0;
    }

    s->found  = 1;
    s->entry  = *e;
    s->sector = sector;
    s->index  = index;
    return 1;
}

/* Resolve a path to its directory entry and that entry's disk location.
 * Root itself has no entry, so a lookup of "/" reports found=0 but
 * ok_root=1. loc_* may be null when the caller only reads. */
static int walk_loc(const char* path, struct dirent_raw* out, int* is_root,
                    uint32_t* loc_sector, uint32_t* loc_index) {
    *is_root = 0;

    if (path[0] != '/') return -1;
    if (path[1] == '\0') { *is_root = 1; return 0; }

    uint16_t dir_cluster = 0;       /* start at root */
    uint32_t i = 1;

    for (;;) {
        uint32_t start = i;
        while (path[i] && path[i] != '/') ++i;
        uint32_t len = i - start;
        if (len == 0) return -1;

        struct search s;
        s.found = 0;
        if (to_83(path + start, len, s.want) != 0) return -1;

        if (iterate_dir(dir_cluster, search_cb, &s) != 0) return -1;
        if (!s.found) return -1;

        if (path[i] == '\0') {
            *out = s.entry;
            if (loc_sector) *loc_sector = s.sector;
            if (loc_index)  *loc_index  = s.index;
            return 0;
        }

        /* More components: this one had better be a directory. */
        if (!(s.entry.attr & ATTR_DIR)) return -1;
        dir_cluster = s.entry.first_cluster;
        ++i;
    }
}

static int walk(const char* path, struct dirent_raw* out, int* is_root) {
    return walk_loc(path, out, is_root, 0, 0);
}

/* Rewrite an entry where it lives. */
static int store_dirent(uint32_t sector, uint32_t index,
                        const struct dirent_raw* e) {
    struct buf* b = bread(sector);
    if (!b) return -1;

    struct dirent_raw* slot = (struct dirent_raw*)&b->data[index * DIRENT_SIZE];
    *slot = *e;
    bwrite(b);
    return 0;
}

static int fat_stat(const char* path, struct vfs_stat* out) {
    if (!mounted) return -1;

    struct dirent_raw e;
    int is_root;
    if (walk(path, &e, &is_root) != 0) return -1;

    if (is_root) {
        out->size   = 0;
        out->is_dir = 1;
    } else {
        out->size   = e.size;
        out->is_dir = (e.attr & ATTR_DIR) ? 1 : 0;
    }
    return 0;
}

static int fat_read(const char* path, uint32_t off, void* buf, uint32_t n) {
    if (!mounted) return -1;

    struct dirent_raw e;
    int is_root;
    if (walk(path, &e, &is_root) != 0) return -1;
    if (is_root || (e.attr & ATTR_DIR)) return -1;

    if (off >= e.size) return 0;
    if (off + n > e.size) n = e.size - off;

    uint32_t cluster_bytes = sectors_per_cluster * 512;
    uint16_t cluster = e.first_cluster;

    /* Skip whole clusters of offset. */
    uint32_t skip = off / cluster_bytes;
    while (skip-- && cluster < CHAIN_END) cluster = fat_entry(cluster);

    uint8_t* out = (uint8_t*)buf;
    uint32_t copied = 0;
    uint32_t pos = off % cluster_bytes;

    while (copied < n && cluster >= 2 && cluster < CHAIN_END) {
        uint32_t sector = cluster_to_sector(cluster) + pos / 512;
        uint32_t within = pos % 512;

        struct buf* b = bread(sector);
        if (!b) return -1;

        uint32_t take = 512 - within;
        if (take > n - copied) take = n - copied;

        for (uint32_t i = 0; i < take; ++i) out[copied + i] = b->data[within + i];

        copied += take;
        pos    += take;

        if (pos >= cluster_bytes) {
            pos = 0;
            cluster = fat_entry(cluster);
        }
    }

    return (int)copied;
}

struct list_ctx {
    vfs_emit_fn emit;
    void*       ctx;
};

static int list_cb(const struct dirent_raw* e, uint32_t sector,
                   uint32_t index, void* opaque) {
    (void)sector; (void)index;
    struct list_ctx* lc = (struct list_ctx*)opaque;

    if (!is_live(e)) return 0;

    char name[13];
    from_83(e->name, name);

    /* vvfat likes to include . and .. - real, but noise here. */
    if (name[0] == '.') return 0;

    lc->emit(name, e->size, (e->attr & ATTR_DIR) ? 1 : 0, lc->ctx);
    return 0;
}

static int fat_list(const char* path, vfs_emit_fn emit, void* ctx) {
    if (!mounted) return -1;

    struct dirent_raw e;
    int is_root;
    if (walk(path, &e, &is_root) != 0) return -1;

    uint16_t cluster;
    if (is_root) {
        cluster = 0;
    } else {
        if (!(e.attr & ATTR_DIR)) return -1;
        cluster = e.first_cluster;
    }

    struct list_ctx lc = { emit, ctx };
    return iterate_dir(cluster, list_cb, &lc);
}

/* ---- write side ---------------------------------------------------------- */

struct free_slot {
    int      found;
    uint32_t sector;
    uint32_t index;
};

static int free_slot_cb(const struct dirent_raw* e, uint32_t sector,
                        uint32_t index, void* ctx) {
    struct free_slot* fs = (struct free_slot*)ctx;

    if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
        fs->found  = 1;
        fs->sector = sector;
        fs->index  = index;
        return 1;
    }
    return 0;
}

/* Split "/DIR/NAME.EXT" into the directory's first cluster (0 for root) and
 * the final component. */
static int parent_of(const char* path, uint16_t* dir_cluster,
                     const char** name_out, uint32_t* name_len) {
    if (path[0] != '/') return -1;

    uint32_t last = 0;
    for (uint32_t i = 1; path[i]; ++i) {
        if (path[i] == '/') last = i;
    }

    *name_out = path + last + 1;
    *name_len = 0;
    while ((*name_out)[*name_len]) (*name_len)++;
    if (*name_len == 0) return -1;

    if (last == 0) {
        *dir_cluster = 0;
        return 0;
    }

    /* Resolve the directory part. */
    char dirpath[FAT_PATH_MAX];
    if (last >= sizeof(dirpath)) return -1;
    for (uint32_t i = 0; i < last; ++i) dirpath[i] = path[i];
    dirpath[last] = '\0';

    struct dirent_raw d;
    int is_root;
    if (walk(dirpath, &d, &is_root) != 0) return -1;
    if (is_root) { *dir_cluster = 0; return 0; }
    if (!(d.attr & ATTR_DIR)) return -1;

    *dir_cluster = d.first_cluster;
    return 0;
}

/* Create empty, or truncate what's there. No directory growth: when every
 * slot is taken the create fails rather than extending the directory -
 * roots have hundreds of slots and that day hasn't come. */
static int fat_create(const char* path) {
    if (!mounted) return -1;

    struct dirent_raw e;
    int is_root;
    uint32_t sector, index;

    if (walk_loc(path, &e, &is_root, &sector, &index) == 0) {
        if (is_root || (e.attr & ATTR_DIR)) return -1;

        /* Truncate: free the data, keep the name. */
        if (e.first_cluster >= 2) free_chain(e.first_cluster);
        e.first_cluster = 0;
        e.size          = 0;
        return store_dirent(sector, index, &e);
    }

    uint16_t dir_cluster;
    const char* name;
    uint32_t name_len;
    if (parent_of(path, &dir_cluster, &name, &name_len) != 0) return -1;

    struct dirent_raw fresh;
    for (uint32_t i = 0; i < sizeof(fresh); ++i) ((uint8_t*)&fresh)[i] = 0;
    if (to_83(name, name_len, fresh.name) != 0) return -1;
    fresh.attr = 0x20;      /* archive, i.e. a plain file */

    struct free_slot slot = { 0, 0, 0 };
    if (iterate_dir(dir_cluster, free_slot_cb, &slot) != 0) return -1;
    if (!slot.found) return -1;

    return store_dirent(slot.sector, slot.index, &fresh);
}

static int fat_write(const char* path, uint32_t off, const void* buf,
                     uint32_t n) {
    if (!mounted) return -1;
    if (n == 0) return 0;

    struct dirent_raw e;
    int is_root;
    uint32_t sector, index;
    if (walk_loc(path, &e, &is_root, &sector, &index) != 0) return -1;
    if (is_root || (e.attr & ATTR_DIR)) return -1;

    uint32_t cluster_bytes = sectors_per_cluster * 512;
    uint32_t need_clusters = (off + n + cluster_bytes - 1) / cluster_bytes;

    /* Make the chain long enough, allocating as we go. An empty file gets
     * its first cluster here, and the dirent learns about it below. */
    uint16_t first = e.first_cluster;
    if (first < 2) {
        first = alloc_cluster();
        if (!first) return -1;
        e.first_cluster = first;
    }

    uint16_t cl = first;
    for (uint32_t i = 1; i < need_clusters; ++i) {
        uint16_t next = fat_entry(cl);
        if (next < 2 || next >= CHAIN_END) {
            next = alloc_cluster();
            if (!next) return -1;
            if (fat_set_entry(cl, next) != 0) return -1;
        }
        cl = next;
    }

    /* Walk to the cluster holding off and copy sector-slices through the
     * cache. */
    cl = first;
    for (uint32_t skip = off / cluster_bytes; skip > 0; --skip) {
        cl = fat_entry(cl);
        if (cl < 2 || cl >= CHAIN_END) return -1;
    }

    const uint8_t* in = (const uint8_t*)buf;
    uint32_t copied = 0;
    uint32_t pos = off % cluster_bytes;

    while (copied < n) {
        uint32_t s      = cluster_to_sector(cl) + pos / 512;
        uint32_t within = pos % 512;

        struct buf* b = bread(s);
        if (!b) return -1;

        uint32_t take = 512 - within;
        if (take > n - copied) take = n - copied;

        for (uint32_t i = 0; i < take; ++i) b->data[within + i] = in[copied + i];
        bwrite(b);

        copied += take;
        pos    += take;

        if (pos >= cluster_bytes && copied < n) {
            pos = 0;
            cl = fat_entry(cl);
            if (cl < 2 || cl >= CHAIN_END) return -1;
        }
    }

    if (off + n > e.size) e.size = off + n;
    if (store_dirent(sector, index, &e) != 0) return -1;

    return (int)copied;
}

static int fat_unlink(const char* path) {
    if (!mounted) return -1;

    struct dirent_raw e;
    int is_root;
    uint32_t sector, index;
    if (walk_loc(path, &e, &is_root, &sector, &index) != 0) return -1;
    if (is_root || (e.attr & ATTR_DIR)) return -1;

    if (e.first_cluster >= 2) free_chain(e.first_cluster);

    e.name[0] = 0xE5;
    return store_dirent(sector, index, &e);
}

static const struct fs_ops fat_ops = {
    .fsname = "fat16",
    .stat   = fat_stat,
    .read   = fat_read,
    .list   = fat_list,
    .create = fat_create,
    .write  = fat_write,
    .unlink = fat_unlink,
};

/* Non-zero if the sector holds a plausible BPB. */
static int parse_bpb(const uint8_t* s, uint32_t base) {
    uint16_t bytes_per_sector = read_u16(s + 11);
    uint8_t  spc              = s[13];
    uint16_t reserved         = read_u16(s + 14);
    uint8_t  nfats            = s[16];
    uint16_t roots            = read_u16(s + 17);
    uint16_t spf              = read_u16(s + 22);

    if (bytes_per_sector != 512) return 0;
    if (spc == 0 || nfats == 0 || spf == 0 || roots == 0) return 0;

    part_start          = base;
    sectors_per_cluster = spc;
    root_entries        = roots;
    num_fats            = nfats;
    sectors_per_fat     = spf;
    fat_start           = base + reserved;
    root_start          = fat_start + (uint32_t)nfats * spf;
    root_sectors        = ((uint32_t)roots * DIRENT_SIZE + 511) / 512;
    data_start          = root_start + root_sectors;

    /* How many clusters the FAT can actually name, bounded by both the FAT
     * size and what fits on the disk. */
    uint32_t total = read_u16(s + 19);
    if (total == 0) total = read_u32(s + 32);
    uint32_t data_sectors = (total > (data_start - base))
                            ? total - (data_start - base) : 0;
    total_clusters = data_sectors / spc;

    uint32_t fat_max = (spf * 512u / 2) - 2;
    if (total_clusters > fat_max) total_clusters = fat_max;

    return 1;
}

/* Format the raw disk as FAT16 and mount it. Everything on the disk dies.
 * Fixed geometry: 4 sectors per cluster, two FATs, a 512-entry root. */
int fat16_format(void) {
    if (!ata_present()) return -1;

    uint32_t total = ata_sector_count();
    if (total > 65535) total = 65535;   /* keep total_sectors16 honest */

    uint8_t  spc      = 4;
    uint16_t reserved = 1;
    uint8_t  nfats    = 2;
    uint16_t roots    = 512;

    /* Sectors per FAT, from the cluster count it has to name. */
    uint32_t clusters = total / spc;
    uint16_t spf = (uint16_t)((clusters * 2 + 511) / 512);

    struct buf* b = bread(0);
    if (!b) return -1;

    for (int i = 0; i < 512; ++i) b->data[i] = 0;

    b->data[0] = 0xEB; b->data[1] = 0x3C; b->data[2] = 0x90;
    const char* oem = "MAXOS   ";
    for (int i = 0; i < 8; ++i) b->data[3 + i] = (uint8_t)oem[i];

    b->data[11] = 0x00; b->data[12] = 0x02;             /* 512 B sectors */
    b->data[13] = spc;
    b->data[14] = (uint8_t)(reserved & 0xFF); b->data[15] = 0;
    b->data[16] = nfats;
    b->data[17] = (uint8_t)(roots & 0xFF); b->data[18] = (uint8_t)(roots >> 8);
    b->data[19] = (uint8_t)(total & 0xFF); b->data[20] = (uint8_t)(total >> 8);
    b->data[21] = 0xF8;                                 /* fixed disk */
    b->data[22] = (uint8_t)(spf & 0xFF); b->data[23] = (uint8_t)(spf >> 8);
    b->data[510] = 0x55; b->data[511] = 0xAA;
    bwrite(b);

    /* Zero the FATs and the root directory; seed the two reserved FAT
     * entries. */
    uint32_t root_secs = ((uint32_t)roots * DIRENT_SIZE + 511) / 512;
    uint32_t meta_end  = reserved + (uint32_t)nfats * spf + root_secs;

    for (uint32_t s = reserved; s < meta_end; ++s) {
        struct buf* z = bread(s);
        if (!z) return -1;
        for (int i = 0; i < 512; ++i) z->data[i] = 0;
        bwrite(z);
    }

    for (uint32_t f = 0; f < nfats; ++f) {
        struct buf* z = bread(reserved + f * spf);
        if (!z) return -1;
        z->data[0] = 0xF8; z->data[1] = 0xFF;
        z->data[2] = 0xFF; z->data[3] = 0xFF;
        bwrite(z);
    }

    if (bflush() != 0) return -1;

    fat16_mount();
    return mounted ? 0 : -1;
}

void fat16_mount(void) {
    mounted = 0;

    if (!ata_present()) return;

    struct buf* b = bread(0);
    if (!b) return;

    if (b->data[510] != 0x55 || b->data[511] != 0xAA) return;

    /* Bare filesystem: BPB right at sector 0. */
    if ((b->data[0] == 0xEB || b->data[0] == 0xE9) && parse_bpb(b->data, 0)) {
        mounted = 1;
    } else {
        /* Partitioned (vvfat does this): first MBR entry points at it. */
        const uint8_t* part = &b->data[0x1BE];
        uint32_t start = read_u32(part + 8);

        if (part[4] != 0 && start != 0) {
            struct buf* pb = bread(start);
            if (pb && (pb->data[0] == 0xEB || pb->data[0] == 0xE9) &&
                parse_bpb(pb->data, start)) {
                mounted = 1;
            }
        }
    }

    if (!mounted) {
        kprintf("fat16: disk present but no filesystem I recognise\n");
        return;
    }

    kprintf("fat16: mounted, partition at %u, %u sectors/cluster, "
            "%u root entries, %u clusters\n",
            part_start, sectors_per_cluster, root_entries, total_clusters);

    /* The VFS has no unmount, so a re-probe (after mkfs) only refreshes the
     * geometry - the vtable is already routed. */
    if (!registered) {
        vfs_mount("/", &fat_ops);
        registered = 1;
    }
}
