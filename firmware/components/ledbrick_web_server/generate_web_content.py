#!/usr/bin/env python3
"""Generate web_content.cpp from web UI files with compression."""

import os
import sys
import gzip

def compress_content(content):
    """Compress content using gzip."""
    return gzip.compress(content.encode('utf-8'))

def bytes_to_c_array(data):
    """Convert bytes to C array initializer."""
    # Create hex string representation
    hex_bytes = []
    for i, byte in enumerate(data):
        if i > 0:
            hex_bytes.append(', ')
            if i % 16 == 0:
                hex_bytes.append('\n    ')
        hex_bytes.append(f'0x{byte:02x}')
    
    return '{\n    ' + ''.join(hex_bytes) + '\n}'

def main():
    web_dir = os.path.join(os.path.dirname(__file__), 'web')
    output_file = os.path.join(os.path.dirname(__file__), 'web_content.cpp')
    
    files = [
        ('index.html', 'INDEX_HTML', 'text/html'),
        ('ledbrick_api.js', 'LEDBRICK_JS', 'application/javascript'),
        ('ledbrick-ui.js', 'LEDBRICK_UI_JS', 'application/javascript'),
        ('style.css', 'STYLE_CSS', 'text/css'),
    ]
    
    with open(output_file, 'w') as out:
        out.write('#include "web_content.h"\n\n')
        out.write('namespace esphome {\n')
        out.write('namespace ledbrick_web_server {\n\n')
        
        # Write metadata
        out.write('// Content metadata\n')
        
        for filename, varname, mime_type in files:
            filepath = os.path.join(web_dir, filename)
            if os.path.exists(filepath):
                with open(filepath, 'r') as f:
                    content = f.read()
                
                # Compress content
                compressed = compress_content(content)
                original_size = len(content)
                compressed_size = len(compressed)
                
                # Write compressed data
                c_array = bytes_to_c_array(compressed)
                out.write(f'// {filename} - Original: {original_size} bytes, Compressed: {compressed_size} bytes ({compressed_size * 100 // original_size}%)\n')
                out.write(f'const uint8_t {varname}_COMPRESSED[] = {c_array};\n')
                out.write(f'const size_t {varname}_SIZE = {compressed_size};\n')
                out.write(f'const char {varname}_TYPE[] = "{mime_type}";\n\n')
            else:
                print(f"Warning: {filepath} not found")
                out.write(f'const uint8_t {varname}_COMPRESSED[] = {0};\n')
                out.write(f'const size_t {varname}_SIZE = 0;\n')
                out.write(f'const char {varname}_TYPE[] = "{mime_type}";\n\n')
        
        out.write('}  // namespace ledbrick_web_server\n')
        out.write('}  // namespace esphome\n')
    
    print(f"Generated {output_file}")

if __name__ == '__main__':
    main()