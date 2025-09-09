#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

// Read built files
const distDir = path.join(__dirname, '../dist');
const htmlFile = path.join(distDir, 'index.html');
const jsFile = path.join(distDir, 'app.js');
const cssFile = path.join(distDir, 'app.css');

// Output C++ file
const outputFile = path.join(__dirname, '../../web_content.cpp');

function fileToHexArray(filePath) {
  const content = fs.readFileSync(filePath);
  const compressed = zlib.gzipSync(content, { level: 9 });
  
  console.log(`${path.basename(filePath)}:`);
  console.log(`  Original: ${content.length} bytes`);
  console.log(`  Compressed: ${compressed.length} bytes (${((compressed.length / content.length) * 100).toFixed(1)}%)`);
  
  const hexArray = [];
  for (let i = 0; i < compressed.length; i++) {
    hexArray.push('0x' + compressed[i].toString(16).padStart(2, '0'));
  }
  
  return { hex: hexArray, size: compressed.length };
}

function updateHtmlForProduction() {
  // Read the built HTML and update script/link tags
  let html = fs.readFileSync(htmlFile, 'utf8');
  
  // The Vite build already updates the HTML with correct paths
  // Just ensure we're serving the right files
  if (!html.includes('app.js')) {
    console.warn('Warning: HTML may not have correct script references');
  }
  
  return html;
}

function generateCppFile() {
  console.log('Generating C++ web content...\n');
  
  // Verify HTML has correct references
  updateHtmlForProduction();
  
  const htmlData = fileToHexArray(htmlFile);
  const jsData = fileToHexArray(jsFile);
  const cssData = fileToHexArray(cssFile);
  
  const totalSize = htmlData.size + jsData.size + cssData.size;
  console.log(`\nTotal compressed size: ${totalSize} bytes (${(totalSize / 1024).toFixed(1)}KB)`);
  
  const cppContent = `// Auto-generated web content - DO NOT EDIT
// Generated on ${new Date().toISOString()}
// Total size: ${totalSize} bytes compressed

#include "web_content.h"
#include <cstring>

// Memory storage attributes
#ifdef USE_ESP_IDF
  // ESP-IDF: const data is automatically stored in flash
  #define PROGMEM_ATTR
#else
  // Arduino: use PROGMEM to store in flash
  #include <pgmspace.h>
  #define PROGMEM_ATTR PROGMEM
#endif

namespace esphome {
namespace ledbrick_web_server {

// index.html (${htmlData.size} bytes compressed)
const uint8_t INDEX_HTML_COMPRESSED[] PROGMEM_ATTR = {
${htmlData.hex.join(', ')}
};
const size_t INDEX_HTML_SIZE = ${htmlData.size};
const char INDEX_HTML_TYPE[] = "text/html";

// app.js - Combined React application (${jsData.size} bytes compressed)
const uint8_t LEDBRICK_JS_COMPRESSED[] PROGMEM_ATTR = {
${jsData.hex.join(', ')}
};
const size_t LEDBRICK_JS_SIZE = ${jsData.size};
const char LEDBRICK_JS_TYPE[] = "application/javascript";

// app.css (${cssData.size} bytes compressed)
const uint8_t STYLE_CSS_COMPRESSED[] PROGMEM_ATTR = {
${cssData.hex.join(', ')}
};
const size_t STYLE_CSS_SIZE = ${cssData.size};
const char STYLE_CSS_TYPE[] = "text/css";

}  // namespace ledbrick_web_server
}  // namespace esphome
`;

  fs.writeFileSync(outputFile, cppContent);
  console.log(`\nGenerated ${outputFile}`);
}

// Check if dist directory exists
if (!fs.existsSync(distDir)) {
  console.error('Error: dist directory not found. Run "npm run build" first.');
  process.exit(1);
}

// Check if all files exist
const requiredFiles = [htmlFile, jsFile, cssFile];
for (const file of requiredFiles) {
  if (!fs.existsSync(file)) {
    console.error(`Error: Required file not found: ${file}`);
    process.exit(1);
  }
}

generateCppFile();