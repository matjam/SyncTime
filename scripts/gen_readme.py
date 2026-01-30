#!/usr/bin/env python3
"""
Convert README.md to Aminet readme format.
Header is hardcoded, content extracted from README.md.
"""

import re
import sys

HEADER = """\
Short:        NTP time sync commodity with timezone support
Author:       chrome@stupendous.net (Nathan Ollerenshaw)
Uploader:     chrome@stupendous.net (Nathan Ollerenshaw)
Type:         util/time
Version:      {version}
Architecture: m68k-amigaos >= 3.2.0
Requires:     AmigaOS 3.2+, bsdsocket.library, TCP/IP stack
Distribution: Aminet
"""


def parse_readme(readme_path):
    """Parse README.md into sections."""
    with open(readme_path, 'r') as f:
        content = f.read()

    sections = {}
    current_section = None
    current_content = []

    for line in content.split('\n'):
        if line.startswith('## '):
            if current_section and current_content:
                sections[current_section] = '\n'.join(current_content).strip()
            current_section = line[3:].strip().lower()
            current_content = []
        elif current_section:
            current_content.append(line)

    if current_section and current_content:
        sections[current_section] = '\n'.join(current_content).strip()

    return sections


def strip_markdown(text):
    """Convert markdown to plain text."""
    text = re.sub(r'```[^`]*```', '', text, flags=re.DOTALL)
    text = re.sub(r'`([^`]+)`', r'\1', text)
    text = re.sub(r'\*\*([^*]+)\*\*', r'\1', text)
    text = re.sub(r'\*([^*]+)\*', r'\1', text)
    text = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', text)
    return text


def format_section(title, content, is_list=False):
    """Format a section."""
    lines = [f'{title}:\n']
    for line in content.split('\n'):
        line = line.strip()
        if not line or line.startswith('```'):
            continue
        if line.startswith('- '):
            text = strip_markdown(line[2:])
            lines.append(f'  * {text}')
        else:
            text = strip_markdown(line)
            if text:
                lines.append(f'  {text}')
    return '\n'.join(lines)


def format_history(content):
    """Format history section."""
    lines = ['History:\n']
    for line in content.split('\n'):
        line = line.strip()
        if line.startswith('- **'):
            match = re.match(r'-\s*\*\*(.+?)\*\*\s*-\s*(.+)', line)
            if match:
                lines.append(f'  {match.group(1)} - {match.group(2)}')
    return '\n'.join(lines)


def generate_readme(readme_path, version):
    """Generate Aminet-style readme."""
    sections = parse_readme(readme_path)
    output = [HEADER.format(version=version)]

    if 'description' in sections:
        output.append(strip_markdown(sections['description']))
        output.append('')

    if 'features' in sections:
        output.append(format_section('Features', sections['features']))
        output.append('')

    if 'installation' in sections:
        output.append(format_section('Installation', sections['installation']))
        output.append('')

    if 'usage' in sections:
        output.append(format_section('Usage', sections['usage']))
        output.append('')

    if 'tooltypes' in sections:
        output.append(format_section('Tooltypes', sections['tooltypes']))
        output.append('')

    if 'requirements' in sections:
        output.append(format_section('Requirements', sections['requirements']))
        output.append('')

    output.append('Source Code:\n')
    output.append('  https://github.com/matjam/synctime')
    output.append('')

    if 'history' in sections:
        output.append(format_history(sections['history']))
        output.append('')

    output.append('License:\n')
    output.append('  MIT License. See LICENSE file.')
    output.append('')

    output.append('Contact:\n')
    output.append('  Nathan Ollerenshaw <chrome@stupendous.net>')

    return '\n'.join(output)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <README.md> <version>", file=sys.stderr)
        sys.exit(1)

    print(generate_readme(sys.argv[1], sys.argv[2]))
