# Contributing to CITS3007 Project (Group 40)

This document outlines the team organization, project setup, and contribution guidelines for our CITS3007 group project. The project is hosted on GitHub, and our primary communication tool is **MS Teams**.

## 1. Team Organization & Task Allocation
Work is **split by feature**. When picking up a new task, ensure it is communicated with the team to avoid duplicated effort. 

**Communication:**
All major discussions, task assignments, and progress updates must be communicated through **MS Teams**. 

**Risk Management:**
In cases of unforeseen risks, such as a team member falling ill or being unavailable, priority tasks will be immediately identified and redistributed among the remaining active group members. Please communicate any absences on MS Teams as early as possible so work can be properly managed.

## 2. Branching Strategy
We use a feature-branch workflow based off `main`. Please use the following naming conventions when creating branches:

- **`feature/<feature-name>`** for new functionality (e.g., `feature/bun-parser`)
- **`bugfix/<bug-name>`** for fixing issues (e.g., `bugfix/memory-leak`)
- **`chore/<task-name>`** for maintenance and config (e.g., `chore/gitignore-update`)

**Example:**
```bash
git switch main
git pull origin main
git switch -c feature/my-new-feature
```

## 3. Commit Guidelines
We use [Conventional Commits](https://www.conventionalcommits.org/). Commit messages should follow this format:
```text
<type>: <description>
```
**Common Types:**
- `feat:` A new feature or significant addition
- `fix:` A bug fix
- `docs:` Documentation changes (e.g., README or Doxygen)
- `test:` Adding or updating tests
- `chore:` Maintenance, build, or setup work (e.g., `chore: add .DS_Store to .gitignore`)

## 4. Coding Standards

### C Formatting
We use `clang-format` to maintain a consistent C coding style. Please ensure your code is formatted before committing. 

Ubuntu/Debian install:
```sh
sudo apt install clang-format
```

### Doxygen Comments
We use **Doxygen** for function and file-level documentation. Every new function or structure needs a brief Doxygen comment explaining its purpose, parameters, and return values.

**Example Doxygen Comment:**
```c
/**
 * @brief Parses the provided .bun file and validates its structure.
 *
 * @param filename The path to the .bun file to read.
 * @return 0 on success, -1 on error.
 */
int parse_bun_file(const char *filename) {
    // ...
}
```

## 5. Testing & Pull Requests
Before you request a review or merge your code, make sure it builds correctly and passes the local test suite.

1. **Test your code:** Use the `Makefile` to build and run existing tests.
   ```bash
   make
   ```
2. **Push your branch:**
   ```bash
   git push -u origin feature/your-feature-name
   ```
3. **Open a Pull Request:** Create a PR targeting the `main` branch on GitHub.
4. **Review:** Notify the team that your PR is ready. Address any requested changes.
5. **Merge:** Once tests pass and you receive approval, merge the PR!
