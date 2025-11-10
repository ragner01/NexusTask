# Contributing to NexusTask

Thank you for your interest in contributing to NexusTask! This document provides guidelines and instructions for contributing.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/yourusername/nexustask.git`
3. Create a branch: `git checkout -b feature/your-feature-name`
4. Make your changes
5. Test your changes: `cd build && cmake .. && make && ./task_engine_demo`
6. Commit your changes: `git commit -m "Add feature: description"`
7. Push to your fork: `git push origin feature/your-feature-name`
8. Open a Pull Request

## Code Style

- Follow C++20 best practices
- Use meaningful variable and function names
- Add comments for complex logic
- Maintain thread safety for public APIs
- Follow the existing code style

## Testing

- Add tests for new features
- Ensure all existing tests pass
- Run performance benchmarks to verify no regressions

## Pull Request Process

1. Update README.md if needed
2. Update documentation for new features
3. Ensure CI passes
4. Request review from maintainers

## Reporting Bugs

When reporting bugs, please include:
- Description of the issue
- Steps to reproduce
- Expected behavior
- Actual behavior
- System information (OS, compiler, version)

## Feature Requests

For feature requests, please:
- Describe the feature clearly
- Explain the use case
- Discuss potential implementation approaches

Thank you for contributing!

