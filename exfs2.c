#include "exfs2.h"
#include <libgen.h>

// This function Opens a directory segment or data-segment
FILE* open_segment(int segment_number, int segment_type, const char* mode) {
    char filename[64];
    if (segment_type == INODE_SEGMENT) {
        snprintf(filename, sizeof(filename), "%s%d", INODE_SEG_PREFIX, segment_number);
    } else {
        snprintf(filename, sizeof(filename), "%s%d", DATA_SEG_PREFIX, segment_number);
    }
    return fopen(filename, mode);
}

// This function creates a new segment of 1 Mb size on disk
int create_new_segment(int segment_number, int segment_type) {
    FILE* fp = open_segment(segment_number, segment_type, "wb");
    if (!fp) {
        perror("Failed to create new segment");
        return -1;
    }

    // Initialize segment with zeros
    char buffer[8192] = {0}; // Write in chunks for efficiency
    size_t remaining = SEGMENT_SIZE;
    
    while (remaining > 0) {
        size_t to_write = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        if (fwrite(buffer, 1, to_write, fp) != to_write) {
            perror("Failed to initialize segment");
            fclose(fp);
            return -1;
        }
        remaining -= to_write;
    }
    
    fflush(fp);
    fclose(fp);
    
    if (segment_type == INODE_SEGMENT && segment_number == 0) {
        // Initialize root directory in the first inode segment
        inode_t root_inode;
        memset(&root_inode, 0, sizeof(root_inode));
        root_inode.type = INODE_DIR;
        root_inode.size = 0;
        root_inode.num_direct = 0;
        root_inode.indirect_block = -1;
        root_inode.double_indirect_block = -1;
        root_inode.triple_indirect_block = -1;
        
        // Mark root inode as used in bitmap
        FILE* fp = open_segment(0, INODE_SEGMENT, "rb+");
        if (!fp) return -1;
        
        uint8_t bitmap[BLOCK_SIZE] = {0};
        set_bit(bitmap, ROOT_DIR_INODE);
        write_bitmap(fp, bitmap, BLOCK_SIZE);
        fclose(fp);
        
        // Write root inode
        write_inode(ROOT_DIR_INODE, &root_inode);
    }
    
    return 0;
}

// This function reads the bitmap from a segment
int read_bitmap(FILE* fp, uint8_t* bitmap, int size) {
    fseek(fp, 0, SEEK_SET);
    return fread(bitmap, 1, size, fp);
}

//This function writes the bit map back to the segment
int write_bitmap(FILE* fp, uint8_t* bitmap, int size) {
    fseek(fp, 0, SEEK_SET);
    return fwrite(bitmap, 1, size, fp);
}

// Indentify the first inode or data block that is marked free (with bit 0) from the bitmap.
int find_free_bit(uint8_t* bitmap, int num_bits) {
    for (int i = 0; i < num_bits; i++) {
        if ((bitmap[i / 8] & (1 << (i % 8))) == 0) {
            return i;
        }
    }
    return -1; // No free bits
}

//Set the bit value to 1 in the bitmao indicating that particular inode is allocated
void set_bit(uint8_t* bitmap, int bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

//sets back the bit value to 0 indicating inode as free in bitmap
void clear_bit(uint8_t* bitmap, int bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

//Finds the first free inode and return its number (creating new inode segment if no free inode is found) 
int allocate_inode() {
    int segment_number = 0;
    
    while (1) {
        FILE* fp = open_segment(segment_number, INODE_SEGMENT, "rb+");
        
        if (!fp) {
            // No segment found, create one
            if (create_new_segment(segment_number, INODE_SEGMENT) != 0) {
                return -1;
            }
            fp = open_segment(segment_number, INODE_SEGMENT, "rb+");
            if (!fp) return -1;
        }

        int num_inodes = (SEGMENT_SIZE - BLOCK_SIZE) / sizeof(inode_t);
        
        uint8_t bitmap[BLOCK_SIZE];
        if (read_bitmap(fp, bitmap, BLOCK_SIZE) != BLOCK_SIZE) {
            fclose(fp);
            return -1;
        }

        int free_inode = find_free_bit(bitmap, num_inodes);
        if (free_inode >= 0) {
            set_bit(bitmap, free_inode);
            write_bitmap(fp, bitmap, BLOCK_SIZE);
            fclose(fp);

            return segment_number * num_inodes + free_inode;  // Global inode number
        }

        fclose(fp);
        segment_number++;  // ➔ Keep moving to next segment
    }

    return -1;  // Should never reach
}

//Read the inode meta data and the pointers
int read_inode(int inode_num, inode_t* out_inode) {
    int num_inodes_per_segment = (SEGMENT_SIZE - BLOCK_SIZE) / sizeof(inode_t);
    int segment_number = inode_num / num_inodes_per_segment;
    int index_in_segment = inode_num % num_inodes_per_segment;

    FILE* fp = open_segment(segment_number, INODE_SEGMENT, "rb");
    if (!fp) return -1;

    // Inodes start after bitmap block
    fseek(fp, BLOCK_SIZE + index_in_segment * sizeof(inode_t), SEEK_SET);
    size_t read_count = fread(out_inode, sizeof(inode_t), 1, fp);
    
    fclose(fp);
    
    return (read_count == 1) ? 0 : -1;
}

//write the metadata to inode 
int write_inode(int inode_num, inode_t* in_inode) {
    int num_inodes_per_segment = (SEGMENT_SIZE - BLOCK_SIZE) / sizeof(inode_t);
    int segment_number = inode_num / num_inodes_per_segment;
    int index_in_segment = inode_num % num_inodes_per_segment;

    FILE* fp = open_segment(segment_number, INODE_SEGMENT, "rb+");
    if (!fp) return -1;

    // Inodes start after bitmap block
    fseek(fp, BLOCK_SIZE + index_in_segment * sizeof(inode_t), SEEK_SET);
    size_t write_count = fwrite(in_inode, sizeof(inode_t), 1, fp);
    
    fclose(fp);
    
    return (write_count == 1) ? 0 : -1;
}

//Clear the inode metadata and make the inode empty 
int free_inode(int inode_num) {
    int num_inodes_per_segment = (SEGMENT_SIZE - BLOCK_SIZE) / sizeof(inode_t);
    int segment_number = inode_num / num_inodes_per_segment;
    int index_in_segment = inode_num % num_inodes_per_segment;

    FILE* fp = open_segment(segment_number, INODE_SEGMENT, "rb+");
    if (!fp) return -1;

    // Read bitmap
    uint8_t bitmap[BLOCK_SIZE];
    read_bitmap(fp, bitmap, BLOCK_SIZE);
    
    // Mark inode as free
    clear_bit(bitmap, index_in_segment);
    write_bitmap(fp, bitmap, BLOCK_SIZE);
    
    fclose(fp);
    return 0;
}

//Identify the first free data block of 4kb in the data segment and 
int allocate_block() {
    int segment_number = 0;

    while (1) {
        FILE* fp = open_segment(segment_number, DATA_SEGMENT, "rb+");

        if (!fp) {
            if (create_new_segment(segment_number, DATA_SEGMENT) != 0) {
                return -1;
            }
            fp = open_segment(segment_number, DATA_SEGMENT, "rb+");
            if (!fp) return -1;
        }

        int num_blocks = (SEGMENT_SIZE - BLOCK_SIZE) / BLOCK_SIZE;

        uint8_t bitmap[BLOCK_SIZE];
        if (read_bitmap(fp, bitmap, BLOCK_SIZE) != BLOCK_SIZE) {
            fclose(fp);
            return -1;
        }

        int free_block = find_free_bit(bitmap, num_blocks);
        if (free_block >= 0) {
            set_bit(bitmap, free_block);
            write_bitmap(fp, bitmap, BLOCK_SIZE);
            fclose(fp);

            return segment_number * num_blocks + free_block;
        }

        fclose(fp);
        segment_number++;  // ➔ Important: keep going to next segment
    }

    return -1;
}

// Copy the 4kb data block into the buffer and read. 
int read_block(int block_id, void* buffer) {
    int blocks_per_segment = (SEGMENT_SIZE - BLOCK_SIZE) / BLOCK_SIZE;
    int segment_number = block_id / blocks_per_segment;
    int block_index = block_id % blocks_per_segment;

    FILE* fp = open_segment(segment_number, DATA_SEGMENT, "rb");
    if (!fp) return -1;

    // Blocks start after bitmap block
    fseek(fp, BLOCK_SIZE + block_index * BLOCK_SIZE, SEEK_SET);
    size_t read_count = fread(buffer, BLOCK_SIZE, 1, fp);
    
    fclose(fp);
    
    return (read_count == 1) ? 0 : -1;
}

//write the data from buffer to data block in a segment
int write_block(int block_id, void* buffer) {
    int blocks_per_segment = (SEGMENT_SIZE - BLOCK_SIZE) / BLOCK_SIZE;
    int segment_number = block_id / blocks_per_segment;
    int block_index = block_id % blocks_per_segment;

    FILE* fp = open_segment(segment_number, DATA_SEGMENT, "rb+");
    if (!fp) return -1;

    // Blocks start after bitmap block
    fseek(fp, BLOCK_SIZE + block_index * BLOCK_SIZE, SEEK_SET);
    size_t write_count = fwrite(buffer, BLOCK_SIZE, 1, fp);
    
    fclose(fp);
    
    return (write_count == 1) ? 0 : -1;
}

// Mark the block as free in its segment bitmap
int free_block(int block_id) {
    int blocks_per_segment = (SEGMENT_SIZE - BLOCK_SIZE) / BLOCK_SIZE;
    int segment_number = block_id / blocks_per_segment;
    int block_index = block_id % blocks_per_segment;

    FILE* fp = open_segment(segment_number, DATA_SEGMENT, "rb+");
    if (!fp) return -1;

    // Read bitmap
    uint8_t bitmap[BLOCK_SIZE];
    read_bitmap(fp, bitmap, BLOCK_SIZE);
    
    // Mark block as free
    clear_bit(bitmap, block_index);
    write_bitmap(fp, bitmap, BLOCK_SIZE);
    
    fclose(fp);
    return 0;
}

// Read a directory block into an entries array
int load_directory_entries(int block_id, dir_entry_t* entries) {
    // Directory entries are stored in data blocks
    char buffer[BLOCK_SIZE];
    
    if (read_block(block_id, buffer) != 0) {
        return -1;
    }
    
    // Copy from block buffer to entries (only copy the actual size of entries)
    memcpy(entries, buffer, sizeof(dir_entry_t) * DIR_ENTRIES_PER_BLOCK);
    
    return 0;
}

// Write the entries array back to its directory block
int save_directory_entries(int block_id, dir_entry_t* entries) {
    // We need to ensure the full block is written, even if entries array is smaller
    char buffer[BLOCK_SIZE] = {0};  // Initialize with zeros
    
    // Copy entries to the beginning of the buffer
    memcpy(buffer, entries, sizeof(dir_entry_t) * DIR_ENTRIES_PER_BLOCK);
    
    // Write the full block
    return write_block(block_id, buffer);
}

// Look for a name inside a directory inode, returns the child's inode number 
int find_entry_in_dir(inode_t* dir_inode, const char* name) {
    if (dir_inode->type != INODE_DIR) {
        return -1; // Not a directory
    }
    
    // Search through all direct blocks of the directory
    for (int i = 0; i < dir_inode->num_direct; i++) {
        dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
        
        if (load_directory_entries(dir_inode->direct_blocks[i], entries) != 0) {
            continue;
        }
        
        // Search through directory entries in this block
        for (unsigned int j = 0; j < DIR_ENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num != -1 && strcmp(entries[j].name, name) == 0) {
                return entries[j].inode_num;
            }
        }
    }
    
    return -1; // Entry not found
}

// Add (name → child_inode_num) into a directory, making a new block if needed
int add_entry_to_dir(inode_t* dir_inode, int dir_inode_num, const char* name, int child_inode_num) {
    if (dir_inode->type != INODE_DIR) {
        return -1; // Not a directory
    }
    
    // First check if the entry already exists
    if (find_entry_in_dir(dir_inode, name) != -1) {
        return -1; // Entry with that name already exists
    }
    
    // Try to find an empty slot in existing blocks
    for (int i = 0; i < dir_inode->num_direct; i++) {
        dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
        
        if (load_directory_entries(dir_inode->direct_blocks[i], entries) != 0) {
            continue;
        }
        
        // Look for an empty slot
        for (unsigned int j = 0; j < DIR_ENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num == -1) {
                // Empty slot found
                strncpy(entries[j].name, name, MAX_FILENAME - 1);
                entries[j].name[MAX_FILENAME - 1] = '\0'; // Ensure null termination
                entries[j].inode_num = child_inode_num;
                
                save_directory_entries(dir_inode->direct_blocks[i], entries);
                return 0;
            }
        }
    }
    
    // No empty slots found, need to allocate new block
    if (dir_inode->num_direct >= MAX_DIRECT_BLOCKS) {
        return -1; // No more direct blocks available (need indirect blocks)
    }
    
    // Allocate new data block
    int new_block_id = allocate_block();
    if (new_block_id == -1) {
        return -1;
    }
    
    // Initialize directory entries
    dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
    // Initialize all entries to zero safely
    memset(entries, 0, sizeof(entries));
    
    // Mark all entries as free
    for (unsigned int i = 0; i < DIR_ENTRIES_PER_BLOCK; i++) {
        entries[i].inode_num = -1; 
    }
    
    // Add new entry
    strncpy(entries[0].name, name, MAX_FILENAME - 1);
    entries[0].name[MAX_FILENAME - 1] = '\0'; // Ensure null termination
    entries[0].inode_num = child_inode_num;
    
    // Save entries
    if (save_directory_entries(new_block_id, entries) != 0) {
        free_block(new_block_id);
        return -1;
    }
    
    // Update directory inode
    dir_inode->direct_blocks[dir_inode->num_direct++] = new_block_id;
    dir_inode->size += BLOCK_SIZE;
    
    // Save directory inode
    write_inode(dir_inode_num, dir_inode);
    
    return 0;
}

// split the directory path
int split_path(const char* path, char parts[][MAX_FILENAME], int* count) {
    *count = 0;
    
    // Handle empty or root path
    if (!path || !path[0] || (path[0] == '/' && path[1] == '\0')) {
        return 0;
    }
    
    char path_copy[MAX_PATH];
    strncpy(path_copy, path, MAX_PATH - 1);
    path_copy[MAX_PATH - 1] = '\0';
    
    // Skip leading slash
    char* start = path_copy;
    if (start[0] == '/') {
        start++;
    }
    
    // Split path by slashes
    char* token = strtok(start, "/");
    while (token != NULL && *count < 32) {  // Limit to avoid overflows
        strncpy(parts[*count], token, MAX_FILENAME - 1);
        parts[*count][MAX_FILENAME - 1] = '\0';
        (*count)++;
        token = strtok(NULL, "/");
    }
    
    return 0;
}

// Copy a local file into the File system at directory path by creating any missing folders
void exfs2_add(const char* exfs2_path, const char* local_file) {
    printf("Adding file '%s' from local path '%s'...\n", exfs2_path, local_file);

    char parts[32][MAX_FILENAME];
    int num_parts = 0;
    split_path(exfs2_path, parts, &num_parts);

    if (num_parts == 0) {
        fprintf(stderr, "Invalid path\n");
        return;
    }

    int current_inode_num = ROOT_DIR_INODE;
    inode_t current_inode;

    if (read_inode(current_inode_num, &current_inode) != 0) {
        fprintf(stderr, "Failed to read root inode\n");
        return;
    }

    // Walk and create intermediate directories
    for (int i = 0; i < num_parts - 1; i++) {
        int next_inode_num = find_entry_in_dir(&current_inode, parts[i]);
        if (next_inode_num == -1) {
            printf("Creating directory: %s\n", parts[i]);

            int new_dir_inode_num = allocate_inode();
            if (new_dir_inode_num == -1) {
                fprintf(stderr, "Failed to allocate inode for directory\n");
                return;
            }

            inode_t new_dir_inode;
            memset(&new_dir_inode, 0, sizeof(new_dir_inode));
            new_dir_inode.type = INODE_DIR;
            new_dir_inode.size = 0;
            new_dir_inode.num_direct = 0;
            new_dir_inode.indirect_block = -1;
            new_dir_inode.double_indirect_block = -1;
            new_dir_inode.triple_indirect_block = -1;

            if (write_inode(new_dir_inode_num, &new_dir_inode) != 0) {
                fprintf(stderr, "Failed to write new directory inode\n");
                return;
            }

            if (add_entry_to_dir(&current_inode, current_inode_num, parts[i], new_dir_inode_num) != 0) {
                fprintf(stderr, "Failed to add new directory entry\n");
                return;
            }

            current_inode_num = new_dir_inode_num;
            memcpy(&current_inode, &new_dir_inode, sizeof(inode_t));
        } else {
            current_inode_num = next_inode_num;
            if (read_inode(current_inode_num, &current_inode) != 0) {
                fprintf(stderr, "Failed to read existing directory inode\n");
                return;
            }

            if (current_inode.type != INODE_DIR) {
                fprintf(stderr, "%s is not a directory\n", parts[i]);
                return;
            }
        }
    }

    const char* filename = parts[num_parts - 1];

    if (find_entry_in_dir(&current_inode, filename) != -1) {
        fprintf(stderr, "File already exists: %s\n", filename);
        return;
    }

    int file_inode_num = allocate_inode();
    if (file_inode_num == -1) {
        fprintf(stderr, "Failed to allocate file inode\n");
        return;
    }

    inode_t file_inode;
    memset(&file_inode, 0, sizeof(file_inode));
    file_inode.type = INODE_FILE;
    file_inode.size = 0;
    file_inode.num_direct = 0;
    file_inode.indirect_block = -1;
    file_inode.double_indirect_block = -1;
    file_inode.triple_indirect_block = -1;

    FILE* local_fp = fopen(local_file, "rb");
    if (!local_fp) {
        perror("Failed to open local file");
        return;
    }

    char buffer[BLOCK_SIZE];
    size_t bytes_read;

    // Buffers for indirect, double, triple
    int indirect_block[BLOCK_SIZE / sizeof(int)] = {0};
    int double_indirect_block[BLOCK_SIZE / sizeof(int)] = {0};

    // Track total blocks separately from direct blocks
    int total_blocks = 0;
    int indirect_count = 0;
    int double_indirect_level1_count = 0;
    int double_indirect_level2_count = 0;

    while ((bytes_read = fread(buffer, 1, BLOCK_SIZE, local_fp)) > 0) {
        int block_id = allocate_block();
        if (block_id == -1) {
            fprintf(stderr, "Failed to allocate data block\n");
            fclose(local_fp);
            return;
        }

        if (bytes_read < BLOCK_SIZE) {
            memset(buffer + bytes_read, 0, BLOCK_SIZE - bytes_read);
        }

        if (write_block(block_id, buffer) != 0) {
            fprintf(stderr, "Failed to write data block\n");
            fclose(local_fp);
            return;
        }

        // Track where to put this block based on total count
        if (total_blocks < MAX_DIRECT_BLOCKS) {
            // Direct block
            file_inode.direct_blocks[file_inode.num_direct++] = block_id;
        } 
        else if (total_blocks < (int)(MAX_DIRECT_BLOCKS + (BLOCK_SIZE / sizeof(int)))) {
            // Single indirect block territory
            if (file_inode.indirect_block == -1) {
                file_inode.indirect_block = allocate_block();
                if (file_inode.indirect_block == -1) {
                    fprintf(stderr, "Failed to allocate indirect block\n");
                    fclose(local_fp);
                    return;
                }
                // Initialize indirect block
                memset(indirect_block, 0, BLOCK_SIZE);
            } else {
                // Read the current indirect block
                read_block(file_inode.indirect_block, indirect_block);
            }

            // Store block in indirect block and write it back
            indirect_block[indirect_count++] = block_id;
            write_block(file_inode.indirect_block, indirect_block);
        } 
        else {
            // Double indirect block territory
            int indirect_blocks_per_block = BLOCK_SIZE / sizeof(int);
            
            // Check if we need to initialize double indirect block
            if (file_inode.double_indirect_block == -1) {
                file_inode.double_indirect_block = allocate_block();
                if (file_inode.double_indirect_block == -1) {
                    fprintf(stderr, "Failed to allocate double indirect block\n");
                    fclose(local_fp);
                    return;
                }
                // Initialize double indirect block
                memset(double_indirect_block, 0, BLOCK_SIZE);
                write_block(file_inode.double_indirect_block, double_indirect_block);
            } else {
                // Read current double indirect block
                read_block(file_inode.double_indirect_block, double_indirect_block);
            }

            // If we need a new level 1 block
            if (double_indirect_level2_count >= indirect_blocks_per_block || double_indirect_block[double_indirect_level1_count] == 0) {
                double_indirect_level2_count = 0;
                
                // Allocate a new level 1 block
                int new_level1_block = allocate_block();
                if (new_level1_block == -1) {
                    fprintf(stderr, "Failed to allocate level 1 block\n");
                    fclose(local_fp);
                    return;
                }
                
                // Update double indirect block with new level 1 block
                double_indirect_block[double_indirect_level1_count++] = new_level1_block;
                
                // Initialize the new level 1 block
                int level1_data[BLOCK_SIZE / sizeof(int)] = {0};
                write_block(new_level1_block, level1_data);
            }

            // Read the current level 1 block
            int level1_block_id = double_indirect_block[double_indirect_level1_count - 1];
            int level1_data[BLOCK_SIZE / sizeof(int)];
            read_block(level1_block_id, level1_data);
            
            // Store block in level 1 block and write it back
            level1_data[double_indirect_level2_count++] = block_id;
            write_block(level1_block_id, level1_data);
            
            // Write updated double indirect block
            write_block(file_inode.double_indirect_block, double_indirect_block);
        }

        // Track total blocks and update file size
        total_blocks++;
        file_inode.size += bytes_read;
    }

    fclose(local_fp);

    if (write_inode(file_inode_num, &file_inode) != 0) {
        fprintf(stderr, "Failed to write file inode\n");
        return;
    }

    if (add_entry_to_dir(&current_inode, current_inode_num, filename, file_inode_num) != 0) {
        fprintf(stderr, "Failed to add file entry\n");
        return;
    }

    printf("File '%s' added successfully.\n", filename);
}

// Print directory contents starting at inode_num
void exfs2_list_recursive(int inode_num, int depth) {
    inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return;
    }

    if (inode.type != INODE_DIR) {
        return;
    }

    for (int i = 0; i < inode.num_direct; i++) {
        dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
        if (load_directory_entries(inode.direct_blocks[i], entries) != 0) {
            continue;
        }

        for (unsigned int j = 0; j < DIR_ENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num != -1) {
                // Indentation
                for (int k = 0; k < depth; k++) {
                    printf("  ");
                }

                printf("%s", entries[j].name);

                inode_t child_inode;
                if (read_inode(entries[j].inode_num, &child_inode) == 0) {
                    if (child_inode.type == INODE_DIR) {
                        printf("/\n");
                        exfs2_list_recursive(entries[j].inode_num, depth + 1);
                    } else {
                        printf("\n");
                    }
                }
            }
        }
    }
}

// Show the whole filesystem tree from the root directory
void exfs2_list() {
    printf("/\n"); // Root
    exfs2_list_recursive(ROOT_DIR_INODE, 1);
}

// Extract the file information stored in the data segments to a file
void exfs2_extract(const char* exfs2_path) {
    // Split path
    char parts[32][MAX_FILENAME];
    int num_parts = 0;
    split_path(exfs2_path, parts, &num_parts);

    if (num_parts == 0) {
        fprintf(stderr, "Invalid path\n");
        return;
    }

    int current_inode_num = ROOT_DIR_INODE;
    inode_t current_inode;
    if (read_inode(current_inode_num, &current_inode) != 0) {
        fprintf(stderr, "Failed to read root inode\n");
        return;
    }

    // Walk through path
    for (int i = 0; i < num_parts; i++) {
        int next_inode_num = find_entry_in_dir(&current_inode, parts[i]);
        if (next_inode_num == -1) {
            fprintf(stderr, "Path not found: %s\n", parts[i]);
            return;
        }
        current_inode_num = next_inode_num;
        if (read_inode(current_inode_num, &current_inode) != 0) {
            fprintf(stderr, "Failed to read inode\n");
            return;
        }
    }

    // Now current_inode should be the file
    if (current_inode.type != INODE_FILE) {
        fprintf(stderr, "Not a regular file\n");
        return;
    }

    char buffer[BLOCK_SIZE];
    int remaining = current_inode.size; // total bytes remaining to write

    // Read direct blocks
    for (int i = 0; i < current_inode.num_direct && remaining > 0; i++) {
        if (read_block(current_inode.direct_blocks[i], buffer) == 0) {
            int to_write = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            fwrite(buffer, 1, to_write, stdout);
            remaining -= to_write;
        }
    }

    // Read single indirect blocks
    if (current_inode.indirect_block != -1 && remaining > 0) {
        int indirect_block[BLOCK_SIZE / sizeof(int)];
        if (read_block(current_inode.indirect_block, indirect_block) == 0) {
            for (size_t i = 0; i < BLOCK_SIZE / sizeof(int) && remaining > 0; i++) {
                if (indirect_block[i] == 0) break;
                if (read_block(indirect_block[i], buffer) == 0) {
                    int to_write = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
                    fwrite(buffer, 1, to_write, stdout);
                    remaining -= to_write;
                }
            }
        }
    }

    // Read double indirect blocks
    if (current_inode.double_indirect_block != -1 && remaining > 0) {
        int double_indirect_block[BLOCK_SIZE / sizeof(int)];
        if (read_block(current_inode.double_indirect_block, double_indirect_block) == 0) {
            for (size_t i = 0; i < BLOCK_SIZE / sizeof(int) && remaining > 0; i++) {
                if (double_indirect_block[i] == 0) break;
                int indirect_block[BLOCK_SIZE / sizeof(int)];
                if (read_block(double_indirect_block[i], indirect_block) == 0) {
                    for (size_t j = 0; j < BLOCK_SIZE / sizeof(int) && remaining > 0; j++) {
                        if (indirect_block[j] == 0) break;
                        if (read_block(indirect_block[j], buffer) == 0) {
                            int to_write = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
                            fwrite(buffer, 1, to_write, stdout);
                            remaining -= to_write;
                        }
                    }
                }
            }
        }
    }

    // Read triple indirect blocks
    if (current_inode.triple_indirect_block != -1 && remaining > 0) {
        int triple_indirect_block[BLOCK_SIZE / sizeof(int)];
        if (read_block(current_inode.triple_indirect_block, triple_indirect_block) == 0) {
            for (size_t i = 0; i < BLOCK_SIZE / sizeof(int) && remaining > 0; i++) {
                if (triple_indirect_block[i] == 0) break;
                int double_indirect_block[BLOCK_SIZE / sizeof(int)];
                if (read_block(triple_indirect_block[i], double_indirect_block) == 0) {
                    for (size_t j = 0; j < BLOCK_SIZE / sizeof(int) && remaining > 0; j++) {
                        if (double_indirect_block[j] == 0) break;
                        int indirect_block[BLOCK_SIZE / sizeof(int)];
                        if (read_block(double_indirect_block[j], indirect_block) == 0) {
                            for (size_t k = 0; k < BLOCK_SIZE / sizeof(int) && remaining > 0; k++) {
                                if (indirect_block[k] == 0) break;
                                if (read_block(indirect_block[k], buffer) == 0) {
                                    int to_write = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
                                    fwrite(buffer, 1, to_write, stdout);
                                    remaining -= to_write;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// Delete everything under inode_num and free its space.
void exfs2_remove_recursive(int inode_num) {
    inode_t inode;
    if (read_inode(inode_num, &inode) != 0) {
        return;
    }

    if (inode.type == INODE_FILE) {
        // Free data blocks
        for (int i = 0; i < inode.num_direct; i++) {
            free_block(inode.direct_blocks[i]);
        }

        // Free indirect blocks
        if (inode.indirect_block != -1) {
            int indirect_block_data[BLOCK_SIZE / sizeof(int)];
            if (read_block(inode.indirect_block, indirect_block_data) == 0) {
                for (size_t i = 0; i < BLOCK_SIZE / sizeof(int); i++) {
                    if (indirect_block_data[i] == 0) break;
                    free_block(indirect_block_data[i]);
                }
            }
            free_block(inode.indirect_block);
        }

        // Free the inode
        free_inode(inode_num);

    } else if (inode.type == INODE_DIR) {
        // Recursively delete directory contents
        for (int i = 0; i < inode.num_direct; i++) {
            dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
            if (load_directory_entries(inode.direct_blocks[i], entries) != 0) continue;
            
            for (unsigned int j = 0; j < DIR_ENTRIES_PER_BLOCK; j++) {
                if (entries[j].inode_num != -1) {
                    exfs2_remove_recursive(entries[j].inode_num);
                }
            }

            free_block(inode.direct_blocks[i]);
        }

        free_inode(inode_num);
    }
}

// Remove a file or folder at exfs2_path from the FS.
void exfs2_remove(const char* exfs2_path) {
    // Split path
    char parts[32][MAX_FILENAME];
    int num_parts = 0;
    split_path(exfs2_path, parts, &num_parts);

    if (num_parts == 0) {
        fprintf(stderr, "Invalid path\n");
        return;
    }

    int current_inode_num = ROOT_DIR_INODE;
    inode_t current_inode;
    if (read_inode(current_inode_num, &current_inode) != 0) {
        fprintf(stderr, "Failed to read root inode\n");
        return;
    }

    // Walk through path up to parent
    for (int i = 0; i < num_parts - 1; i++) {
        int next_inode_num = find_entry_in_dir(&current_inode, parts[i]);
        if (next_inode_num == -1) {
            fprintf(stderr, "Path not found: %s\n", parts[i]);
            return;
        }
        current_inode_num = next_inode_num;
        if (read_inode(current_inode_num, &current_inode) != 0) {
            fprintf(stderr, "Failed to read inode\n");
            return;
        }
    }

    // Now current_inode is parent dir, find child to delete
    int target_inode_num = find_entry_in_dir(&current_inode, parts[num_parts-1]);
    if (target_inode_num == -1) {
        fprintf(stderr, "File not found: %s\n", parts[num_parts-1]);
        return;
    }

    exfs2_remove_recursive(target_inode_num);

    // Remove entry from directory
    for (int i = 0; i < current_inode.num_direct; i++) {
        dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
        if (load_directory_entries(current_inode.direct_blocks[i], entries) != 0) continue;

        for (unsigned int j = 0; j < DIR_ENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num == target_inode_num) {
                entries[j].inode_num = -1;
                memset(entries[j].name, 0, MAX_FILENAME);
                save_directory_entries(current_inode.direct_blocks[i], entries);
                break;
            }
        }
    }

    printf("Removed %s successfully.\n", parts[num_parts-1]);
}

// Print a human-readable dump of the structures along exfs2_path.
void exfs2_debug(const char* exfs2_path) {
    printf("Debugging path: %s\n", exfs2_path);

    // Split path
    char parts[32][MAX_FILENAME];
    int num_parts = 0;
    split_path(exfs2_path, parts, &num_parts);

    int current_inode_num = ROOT_DIR_INODE;
    inode_t current_inode;

    if (read_inode(current_inode_num, &current_inode) != 0) {
        fprintf(stderr, "Failed to read root inode \n");
        return;
    }

    printf("directory '/':\n");
    for (int i = 0; i < current_inode.num_direct; i++) {
        dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
        if (load_directory_entries(current_inode.direct_blocks[i], entries) != 0) continue;
        
        for (unsigned int j = 0; j < DIR_ENTRIES_PER_BLOCK; j++) {
            if (entries[j].inode_num != -1) {
                printf("  '%s' %d\n", entries[j].name, entries[j].inode_num);
            }
        }
    }

    for (int d = 0; d < num_parts; d++) {
        int next_inode_num = find_entry_in_dir(&current_inode, parts[d]);
        if (next_inode_num == -1) {
            printf("Component not found: %s\n", parts[d]);
            return;
        }

        current_inode_num = next_inode_num;
        if (read_inode(current_inode_num, &current_inode) != 0) {
            fprintf(stderr, "Failed to read inode\n");
            return;
        }

        if (current_inode.type == INODE_DIR) {
            printf("directory '%s':\n", parts[d]);
            for (int i = 0; i < current_inode.num_direct; i++) {
                dir_entry_t entries[DIR_ENTRIES_PER_BLOCK];
                if (load_directory_entries(current_inode.direct_blocks[i], entries) != 0) continue;
                
                for (unsigned int j = 0; j < DIR_ENTRIES_PER_BLOCK; j++) {
                    if (entries[j].inode_num != -1) {
                        printf("  '%s' %d\n", entries[j].name, entries[j].inode_num);
                    }
                }
            }
        } else if (current_inode.type == INODE_FILE) {
            printf("\nfile '%s':\n", parts[d]);
            printf("  size: %zu bytes\n", current_inode.size);
            printf("Blocks summary:\n");
            
            // Direct blocks summary
            if (current_inode.num_direct > 0) {
                printf("    direct blocks: %d (from %d to %d)\n", 
                       current_inode.num_direct,
                       current_inode.direct_blocks[0],
                       current_inode.direct_blocks[current_inode.num_direct - 1]);
            } else {
                printf("     direct blocks: 0\n");
            }
            
            // Indirect blocks summary
            int indirect_count = 0;
            int first_indirect = -1;
            int last_indirect = -1;
            
            if (current_inode.indirect_block != -1) {
                int indirect_data[BLOCK_SIZE / sizeof(int)];
                if (read_block(current_inode.indirect_block, indirect_data) == 0) {
                    // Count and find first/last blocks
                    for (unsigned int j = 0; j < BLOCK_SIZE / sizeof(int); j++) {
                        if (indirect_data[j] != 0) {
                            if (first_indirect == -1) first_indirect = indirect_data[j];
                            last_indirect = indirect_data[j];
                            indirect_count++;
                        }
                    }
                }
                
                printf("    indirect blocks: %d (from %d to %d) via indirect block %d\n",
                       indirect_count, 
                       first_indirect != -1 ? first_indirect : 0, 
                       last_indirect != -1 ? last_indirect : 0,
                       current_inode.indirect_block);
            } else {
                printf("    indirect blocks: 0\n");
            }
            
            // Double indirect blocks summary
            int double_indirect_count = 0;
            int first_double_indirect = -1;
            int last_double_indirect = -1;
            int level1_count = 0;
            
            if (current_inode.double_indirect_block != -1) {
                int double_indirect_data[BLOCK_SIZE / sizeof(int)];
                if (read_block(current_inode.double_indirect_block, double_indirect_data) == 0) {
                    // Count level-1 blocks
                    for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(int); i++) {
                        if (double_indirect_data[i] != 0) {
                            level1_count++;
                            
                            // Read level-1 block to find data blocks
                            int level1_data[BLOCK_SIZE / sizeof(int)];
                            if (read_block(double_indirect_data[i], level1_data) == 0) {
                                for (unsigned int j = 0; j < BLOCK_SIZE / sizeof(int); j++) {
                                    if (level1_data[j] != 0) {
                                        if (first_double_indirect == -1) first_double_indirect = level1_data[j];
                                        last_double_indirect = level1_data[j];
                                        double_indirect_count++;
                                    }
                                }
                            }
                        }
                    }
                }
                
                printf("    double indirect blocks: %d (from %d to %d) \n",
                       double_indirect_count,
                       first_double_indirect != -1 ? first_double_indirect : 0,
                       last_double_indirect != -1 ? last_double_indirect : 0);
            }
            
            // Triple indirect blocks summary
            int triple_indirect_count = 0;
            int first_triple_indirect = -1;
            int last_triple_indirect = -1;
            int triple_level1_count = 0;
            int triple_level2_count = 0;
            
            if (current_inode.triple_indirect_block != -1) {
                int triple_indirect_data[BLOCK_SIZE / sizeof(int)];
                if (read_block(current_inode.triple_indirect_block, triple_indirect_data) == 0) {
                    // Count level-1 blocks
                    for (unsigned int i = 0; i < BLOCK_SIZE / sizeof(int); i++) {
                        if (triple_indirect_data[i] != 0) {
                            triple_level1_count++;
                            
                            // Read level-1 blocks to find level-2 blocks
                            int level1_data[BLOCK_SIZE / sizeof(int)];
                            if (read_block(triple_indirect_data[i], level1_data) == 0) {
                                for (unsigned int j = 0; j < BLOCK_SIZE / sizeof(int); j++) {
                                    if (level1_data[j] != 0) {
                                        triple_level2_count++;
                                        
                                        // Read level-2 blocks to find data blocks
                                        int level2_data[BLOCK_SIZE / sizeof(int)];
                                        if (read_block(level1_data[j], level2_data) == 0) {
                                            for (unsigned int k = 0; k < BLOCK_SIZE / sizeof(int); k++) {
                                                if (level2_data[k] != 0) {
                                                    if (first_triple_indirect == -1) first_triple_indirect = level2_data[k];
                                                    last_triple_indirect = level2_data[k];
                                                    triple_indirect_count++;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                printf("    triple indirect blocks: %d (from %d to %d) \n",
                       triple_indirect_count,
                       first_triple_indirect != -1 ? first_triple_indirect : 0,
                       last_triple_indirect != -1 ? last_triple_indirect : 0);
            } 
            
        } else {
            printf("Unknown inode type\n");
            return;
        }
    }
}

// Create the first inode/data segments and root dir if they don’t exist yet.
int init_fs() {
    FILE* fp = fopen(INODE_SEG_PREFIX "0", "rb");
    if (fp) {
        // inode_seg_0 already exists
        fclose(fp);
        return 0;
    }

    // inode_seg_0 does not exist, create initial inode and data segments
    if (create_new_segment(0, INODE_SEGMENT) != 0) {
        fprintf(stderr, "Failed to create inode segment 0\n");
        return -1;
    }
    if (create_new_segment(0, DATA_SEGMENT) != 0) {
        fprintf(stderr, "Failed to create data segment 0\n");
        return -1;
    }

    printf("Initialized new file system (root directory created).\n");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  -l                  List the file system contents\n");
        printf("  -a <exfs2_path> -f <local_file>  Add file\n");
        printf("  -r <exfs2_path>     Remove file/directory\n");
        printf("  -e <exfs2_path>     Extract file to stdout\n");
        printf("  -D <exfs2_path>     Debug path\n");
        return 1;
    }

    /*** VERY IMPORTANT: INIT FILE SYSTEM ***/
    if (init_fs() != 0) {
        fprintf(stderr, "Failed to initialize file system\n");
        return 1;
    }

    if (strcmp(argv[1], "-l") == 0) {
        exfs2_list();
    } 
    else if (strcmp(argv[1], "-a") == 0) {
        if (argc != 5 || strcmp(argv[3], "-f") != 0) {
            fprintf(stderr, "Usage: %s -a <exfs2_path> -f <local_file>\n", argv[0]);
            return 1;
        }
        exfs2_add(argv[2], argv[4]);
    }
    else if (strcmp(argv[1], "-r") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s -r <exfs2_path>\n", argv[0]);
            return 1;
        }
        exfs2_remove(argv[2]);
    }
    else if (strcmp(argv[1], "-e") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s -e <exfs2_path>\n", argv[0]);
            return 1;
        }
        exfs2_extract(argv[2]);
    }
    else if (strcmp(argv[1], "-D") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: %s -D <exfs2_path>\n", argv[0]);
            return 1;
        }
        exfs2_debug(argv[2]);
    }
    else {
        printf("Unknown option: %s\n", argv[1]);
        return 1;
    }

    return 0;
}