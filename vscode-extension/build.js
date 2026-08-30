#!/usr/bin/env node

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const extensionDir = __dirname;
const vsixFile = path.join(extensionDir, 'hosc-debug-0.1.0.vsix');

console.log('Building HOSC Debug Extension...');

try {
    // Step 1: Install dependencies
    console.log('Installing dependencies...');
    execSync('npm install', { cwd: extensionDir, stdio: 'inherit' });

    // Step 2: Compile TypeScript
    console.log('Compiling TypeScript...');
    execSync('npm run compile', { cwd: extensionDir, stdio: 'inherit' });

    // Step 3: Package extension
    console.log('Packaging extension...');
    
    // Check if vsce is installed
    try {
        execSync('npx vsce --version', { stdio: 'pipe' });
    } catch (e) {
        console.log('Installing vsce (VS Code Extension Manager)...');
        execSync('npm install -g @vscode/vsce', { stdio: 'inherit' });
    }

    // Package the extension
    execSync('npx vsce package', { cwd: extensionDir, stdio: 'inherit' });

    console.log('\n✓ Extension built successfully!');
    console.log(`  Output: ${vsixFile}`);
    console.log('\nTo install the extension:');
    console.log(`  code --install-extension ${vsixFile}`);
    console.log('\nOr in VS Code:');
    console.log('  1. Go to Extensions view');
    console.log('  2. Click "..." menu');
    console.log('  3. Select "Install from VSIX..."');
    console.log(`  4. Choose: ${vsixFile}`);

} catch (error) {
    console.error('Build failed:', error.message);
    process.exit(1);
}