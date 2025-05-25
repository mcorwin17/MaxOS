// --------------------------------------------------
// File: kernel.c
// Description: Entry point for MaxOS kernel
// Writes a character directly to video memory
// --------------------------------------------------
void main() {
    // Pointer to video memory start (text mode at 0xb8000)
    char* video_memory = (char*) 0xb8000;
    // Write character 'A' to the top-left screen cell
    *video_memory = 'A';
}