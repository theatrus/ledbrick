#!/usr/bin/env python3
"""Generate web_content.cpp from web UI files."""

import os
import sys

def escape_cpp_string(content):
    """Escape content for C++ string literal."""
    # Replace backslashes first
    content = content.replace('\\', '\\\\')
    # Replace quotes
    content = content.replace('"', '\\"')
    # Replace newlines with literal \n
    content = content.replace('\n', '\\n')
    # Replace tabs
    content = content.replace('\t', '\\t')
    return content

def main():
    web_dir = os.path.join(os.path.dirname(__file__), 'web')
    output_file = os.path.join(os.path.dirname(__file__), 'web_content.cpp')
    
    files = [
        ('index.html', 'INDEX_HTML'),
        ('ledbrick_api.js', 'LEDBRICK_JS'),
        ('style.css', 'STYLE_CSS'),
    ]
    
    with open(output_file, 'w') as out:
        out.write('#include "web_content.h"\n\n')
        out.write('namespace esphome {\n')
        out.write('namespace ledbrick_web_server {\n\n')
        
        for filename, varname in files:
            filepath = os.path.join(web_dir, filename)
            if os.path.exists(filepath):
                with open(filepath, 'r') as f:
                    content = f.read()
                
                escaped = escape_cpp_string(content)
                out.write(f'const char {varname}[] = "{escaped}";\n\n')
            else:
                print(f"Warning: {filepath} not found")
                out.write(f'const char {varname}[] = "";\n\n')
        
        out.write('}  // namespace ledbrick_web_server\n')
        out.write('}  // namespace esphome\n')
    
    print(f"Generated {output_file}")

if __name__ == '__main__':
    main()