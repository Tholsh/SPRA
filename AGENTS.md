# Execution environment

Run builds, programs, pipelines, and tests in Linux Bash. On Windows use the
Ubuntu WSL 2 distribution, with Linux-installed compilers and utilities.
Do not add PowerShell launchers, MSYS2 paths, Windows executables, or cmd.exe
redirection workarounds. See docs/tools.md for setup and validation commands.

If the host tool starts in PowerShell, invoke commands through
`wsl -d Ubuntu -- bash -lc 'cd /mnt/c/Users/Theo/Documents/Programs/SPRA && ...'`.
Use `make` to build all programs and `make test` for regression checks.
