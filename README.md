# PBN_TAIL_CLONE

A clone of the Linux [`tail(1)`](https://man7.org/linux/man-pages/man1/tail.1.html) utility written in C.

## Features

| Option | Description |
|--------|-------------|
| _(no option)_ | Print the last **10 lines** of each file |
| `-n NUM` | Print the last *NUM* lines |
| `-n +NUM` | Print from line *NUM* to end of file |
| `-c NUM` | Print the last *NUM* bytes |
| `-c +NUM` | Print from byte *NUM* to end of file |
| `-f` | Follow — keep reading as the file grows (like `tail -f`) |
| `-q` | Quiet — never print filename headers |
| `-v` | Verbose — always print filename headers |
| `--` | End of options; remaining arguments are treated as file names |

When no file is given (or when `-` is used as a file name), **stdin** is read.  
Multiple files are supported; headers (`==> file <==`) are printed automatically.

## Build

```sh
make
```

This produces a `tail` binary in the current directory.

## Usage

```sh
# Last 10 lines of a file (default)
./tail file.txt

# Last 5 lines
./tail -n 5 file.txt

# From line 3 to end
./tail -n +3 file.txt

# Last 100 bytes
./tail -c 100 file.txt

# Follow a log file
./tail -f /var/log/syslog

# Multiple files
./tail -n 2 file1.txt file2.txt

# Read from stdin
cat file.txt | ./tail -n 3
```

## Test

```sh
make test
```

Runs the shell-based test suite in `tests/run_tests.sh` (23 tests).

## Clean

```sh
make clean
```
