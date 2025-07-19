## Overview

ExFS2 is an extensible file system implementation that uses fixed-size 1MB segment files to store both filesystem metadata (inodes) and file data. It supports common file system operations including directory creation, file management, and content listing.

## Features

- ✅ **Inode Management** - Allocation, freeing, reading, and writing
- ✅ **Data Block Management** - Efficient block allocation and deallocation
- ✅ **Directory Operations** - Creation, traversal, and entry management
- ✅ **File Operations** - Adding, reading, and removing files
- ✅ **Block Pointer Support**
  - 1017 direct block pointers per inode (4KB blocks)
  - Single indirect block pointer
  - Double indirect block pointer  
  - Triple indirect block pointer
- ✅ **Automatic Segment Creation** - New segments created when existing ones are full

## Architecture

### Components

| Component | Description |
|-----------|-------------|
| **Inode Segments** | Store inodes and directory metadata |
| **Data Segments** | Store actual file data blocks |
| **Inodes** | File metadata with 1017 direct block pointers (4096 bytes each) |
| **Directories** | Special files mapping filenames to inodes |
| **Bitmap System** | Track free/used inodes and data blocks in each 1MB segment |

## Installation

### Prerequisites
- GCC compiler
- Make utility

### Build Instructions

```bash
# Compile the source code
make

# Clean build files
make clean
```

## Usage

```bash
./exfs2 [OPTION] [ARGS]
```

### Command Options

| Option | Description |
|--------|-------------|
| `-l` | List contents of the file system |
| `-a PATH -f LOCAL_PATH` | Add file at LOCAL_PATH to PATH in the file system |
| `-r PATH` | Remove file or directory at PATH |
| `-e PATH` | Extract file at PATH to stdout |
| `-D PATH` | Show debug information about PATH |

### Example Commands

```bash
# List file system contents
./exfs2 -l

# Add a file to the file system
./exfs2 -a /dir1/file.txt /path/to/local/file.txt

# Remove a file
./exfs2 -r /dir1/file.txt

# Extract a file
./exfs2 -e /dir1/file.txt > output.txt

# Get debug information
./exfs2 -D /dir1/file.txt
```

## Testing

### Test Cases Performed

1. **Small File Test (32 bytes)**
   - Command: `echo "Hey, This is sample"`
   - Result: ✅ Directories created correctly, 1 data block used

2. **Medium File Test (44 KB)**
   - File: C source code file
   - Result: ✅ 11 data blocks used, sequential allocation verified

3. **Large File Test (12 MB)**
   - File: Data mining textbook
   - Result: ✅ 3079 data blocks used
     - 1017 direct block pointers
     - 1024 indirect pointers  
     - 1038 double indirect pointers
   - Verification: ✅ File extraction and diff comparison showed no changes

4. **Very Large File Test (4 GB)**
   - Purpose: Test triple indirect pointer functionality
   - Result: ✅ Functional but slow (180 minutes processing time)

### Quick Test Procedure

```bash
# 1. Create a test file
echo "Content of the test file" > testfile1.txt

# 2. Add to file system
./exfs2 -a /a/b/c/test.txt testfile1.txt

# 3. Verify structure
./exfs2 -l

# 4. Extract and verify
./exfs2 -e /a/b/c/test.txt > extracted.txt
diff testfile1.txt extracted.txt

# 5. Clean up (optional)
./exfs2 -r /a/b/c/test.txt
```

### Verification Commands

```bash
# Compare original with extracted file
diff testfile1.txt <(./exfs2 -e /a/b/c/test.txt)

# Alternative comparison method
./exfs2 -e /a/b/c/test.txt > extracted.txt
diff testfile1.txt extracted.txt
```

> **Note:** If `diff` produces no output, the files are identical.

## Known Limitations

- ⚠️ **Performance Issue**: Large files (4GB+) require significant processing time
  - Triple indirect pointer operations can take up to 180 minutes
  - Future optimization planned for better performance

## Project Structure

```
exfs2/
├── src/           # Source code files
├── Makefile       # Build configuration
├── README.md      # This file
└── segments/      # Generated segment files (created at runtime)
```

## Contributing

This is an academic project for CS514. For questions or issues, please contact the group members listed above.

## License

This project is part of coursework for CS514 at SIUE and is intended for educational purposes.
