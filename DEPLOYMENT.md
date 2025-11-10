# NexusTask Deployment Guide

## Prerequisites

- Git installed
- GitHub account
- SSH key configured (optional, but recommended)

## Steps to Deploy to GitHub

### 1. Create a New Repository on GitHub

1. Go to https://github.com/new
2. Repository name: `nexustask` (or your preferred name)
3. Description: "High-Performance Distributed Task Execution Engine in C++20"
4. Choose Public or Private
5. **DO NOT** initialize with README, .gitignore, or license (we already have these)
6. Click "Create repository"

### 2. Add Remote and Push

```bash
cd /Users/omotolaalimi/Desktop/C++

# Add your GitHub repository as remote
git remote add origin https://github.com/YOUR_USERNAME/nexustask.git

# Or if using SSH:
# git remote add origin git@github.com:YOUR_USERNAME/nexustask.git

# Rename branch to main if needed
git branch -M main

# Push to GitHub
git push -u origin main
```

### 3. Verify Deployment

Visit `https://github.com/YOUR_USERNAME/nexustask` to verify your code is uploaded.

### 4. Set Up GitHub Pages (Optional)

If you want to host documentation:

1. Go to Settings > Pages
2. Source: Deploy from a branch
3. Branch: main, folder: /docs
4. Save

### 5. Enable GitHub Actions

The CI/CD pipeline will automatically run on:
- Push to main/master/develop branches
- Pull requests to main/master/develop branches

You can view workflow runs in the "Actions" tab.

## Updating the Repository

After making changes:

```bash
git add .
git commit -m "Description of changes"
git push origin main
```

## Creating Releases

1. Go to Releases > Create a new release
2. Tag version: `v1.0.0`
3. Release title: `v1.0.0 - Initial Release`
4. Description: See CHANGELOG.md or describe features
5. Publish release

## Repository Settings Recommendations

- Enable Issues
- Enable Discussions (optional)
- Set up branch protection rules for main branch
- Add topics: `cpp`, `c++20`, `task-scheduler`, `distributed-systems`, `lock-free`, `high-performance`

