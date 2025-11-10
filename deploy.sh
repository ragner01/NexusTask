#!/bin/bash
# NexusTask GitHub Deployment Script

echo "🚀 NexusTask GitHub Deployment"
echo "================================"
echo ""

# Check if remote already exists
if git remote get-url origin &>/dev/null; then
    echo "⚠️  Remote 'origin' already exists:"
    git remote get-url origin
    echo ""
    read -p "Do you want to update it? (y/n) " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Deployment cancelled."
        exit 1
    fi
fi

# Get GitHub username
read -p "Enter your GitHub username: " GITHUB_USER
if [ -z "$GITHUB_USER" ]; then
    echo "❌ GitHub username is required!"
    exit 1
fi

# Repository name
REPO_NAME="nexustask"

echo ""
echo "📋 Repository Details:"
echo "   Username: $GITHUB_USER"
echo "   Repository: $REPO_NAME"
echo ""

# Check if repository exists
echo "⚠️  IMPORTANT: Make sure you've created the repository on GitHub first!"
echo "   Go to: https://github.com/new"
echo "   Repository name: $REPO_NAME"
echo "   Description: ⚡ High-Performance Distributed Task Execution Engine in C++20"
echo "   DO NOT initialize with README, .gitignore, or license"
echo ""
read -p "Have you created the repository? (y/n) " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Please create the repository first, then run this script again."
    exit 1
fi

# Choose protocol
echo ""
echo "Choose connection method:"
echo "1) HTTPS (recommended)"
echo "2) SSH"
read -p "Enter choice (1 or 2): " PROTOCOL

if [ "$PROTOCOL" = "2" ]; then
    REMOTE_URL="git@github.com:$GITHUB_USER/$REPO_NAME.git"
else
    REMOTE_URL="https://github.com/$GITHUB_USER/$REPO_NAME.git"
fi

# Add remote
echo ""
echo "🔗 Adding remote repository..."
if git remote get-url origin &>/dev/null; then
    git remote set-url origin "$REMOTE_URL"
else
    git remote add origin "$REMOTE_URL"
fi

# Ensure we're on main branch
git branch -M main

# Push
echo ""
echo "📤 Pushing to GitHub..."
if git push -u origin main; then
    echo ""
    echo "✅ Successfully deployed to GitHub!"
    echo ""
    echo "🌐 View your repository at:"
    echo "   https://github.com/$GITHUB_USER/$REPO_NAME"
    echo ""
    echo "📊 Next steps:"
    echo "   1. Add repository topics: cpp, c++20, task-scheduler, distributed-systems"
    echo "   2. Check GitHub Actions tab for CI/CD status"
    echo "   3. Consider adding a GitHub Pages site for documentation"
else
    echo ""
    echo "❌ Push failed. Please check:"
    echo "   1. Repository exists on GitHub"
    echo "   2. You have push access"
    echo "   3. Your credentials are configured"
    exit 1
fi

