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

/* Everything in sectors, absolute on the disk. */
static int      mounted;
static uint32_t part_start;
static uint32_t fat_start;
static uint32_t root_start;
static uint32_t root_sectors;
static uint32_t data_start;
static uint32_t sectors_per_cluster;
static uint32_t root_entries;

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
 * otherwise - handing each live entry to cb. cb returns non-zero to stop. */
typedef int (*dirent_cb)(const struct dirent_raw* e, void* ctx);

static int iterate_dir(uint16_t cluster, dirent_cb cb, void* ctx) {
    if (cluster == 0) {
        for (uint32_t s = 0; s < root_sectors; ++s) {
            struct buf* b = bread(root_start + s);
            if (!b) return -1;

            for (uint32_t i = 0; i < 512 / DIRENT_SIZE; ++i) {
                const struct dirent_raw* e =
                    (const struct dirent_raw*)&b->data[i * DIRENT_SIZE];

                if (e->name[0] == 0x00) return 0;       /* end of dir */
                if (e->name[0] == 0xE5) continue;       /* deleted */
                if (e->attr == ATTR_LFN) continue;      /* long name entry */
                if (e->attr & ATTR_VOLUME) continue;

                if (cb(e, ctx)) return 0;
            }
        }
        return 0;
    }

    while (cluster < CHAIN_END && cluster >= 2) {
        for (uint32_t s = 0; s < sectors_per_cluster; ++s) {
            struct buf* b = bread(cluster_to_sector(cluster) + s);
            if (!b) return -1;

            for (uint32_t i = 0; i < 512 / DIRENT_SIZE; ++i) {
                const struct dirent_raw* e =
                    (const struct dirent_raw*)&b->data[i * DIRENT_SIZE];

                if (e->name[0] == 0x00) return 0;
                if (e->name[0] == 0xE5) continue;
                if (e->attr == ATTR_LFN) continue;
                if (e->attr & ATTR_VOLUME) continue;

                if (cb(e, ctx)) return 0;
            }
        }
        cluster = fat_entry(cluster);
    }

    return 0;
}

struct search {
    uint8_t want[11];
    int     found;
    struct dirent_raw entry;
};

static int search_cb(const struct dirent_raw* e, void* ctx) {
    struct search* s = (struct search*)ctx;

    for (int i = 0; i < 11; ++i) {
        if (e->name[i] != s->want[i]) return 0;
    }

    s->found = 1;
    s->entry = *e;
    return 1;
}

/* Resolve a path to its directory entry. Root itself has no entry, so a
 * lookup of "/" reports found=0 but ok_root=1. */
static int walk(const char* path, struct dirent_raw* out, int* is_root) {
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
            return 0;
        }

        /* More components: this one had better be a directory. */
        if (!(s.entry.attr & ATTR_DIR)) return -1;
        dir_cluster = s.entry.first_cluster;
        ++i;
    }
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

static int list_cb(const struct dirent_raw* e, void* opaque) {
    struct list_ctx* lc = (struct list_ctx*)opaque;

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

static const struct fs_ops fat_ops = {
    .fsname = "fat16",
    .stat   = fat_stat,
    .read   = fat_read,
    .list   = fat_list,
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
    fat_start           = base + reserved;
    root_start          = fat_start + (uint32_t)nfats * spf;
    root_sectors        = ((uint32_t)roots * DIRENT_SIZE + 511) / 512;
    data_start          = root_start + root_sectors;

    return 1;
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
            "%u root entries\n",
            part_start, sectors_per_cluster, root_entries);

    vfs_mount("/", &fat_ops);
}
