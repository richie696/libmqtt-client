#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
API文档生成脚本
从C++头文件中提取API信息并生成Markdown格式的文档
"""

import re
import os
import sys
import argparse
from pathlib import Path
from typing import List, Dict, Optional
from datetime import datetime


class APIDocGenerator:
    """API文档生成器"""
    
    def __init__(self, source_dir: str, output_dir: str, version: str):
        self.source_dir = Path(source_dir)
        self.output_dir = Path(output_dir)
        self.version = version
        self.include_dir = self.source_dir / "include" / "mqtt_client"
        
    def parse_function(self, content: str) -> List[Dict]:
        """解析函数声明"""
        functions = []
        seen = set()  # 避免重复
        
        # 匹配函数声明：返回类型 函数名(参数列表);
        # 排除在注释、宏定义、条件编译中的匹配
        pattern = r'(?<!//)(?<!/\*)(?<!#define)(?<!#if)(?<!#ifdef)(?<!#ifndef)\s*(\w+(?:\s*\*|\s+))?\s*(\w+)\s*\(([^)]*)\)\s*;'
        
        for match in re.finditer(pattern, content, re.MULTILINE):
            return_type = match.group(1).strip() if match.group(1) else "void"
            func_name = match.group(2)
            
            # 跳过宏定义和条件编译中的内容
            start_pos = match.start()
            # 检查是否在注释中
            comment_before = content[max(0, start_pos-100):start_pos]
            if '/*' in comment_before or '//' in comment_before:
                continue
            
            # 跳过重复的函数
            if func_name in seen:
                continue
            seen.add(func_name)
            
            params_str = match.group(3).strip()
            
            # 解析参数
            params = []
            if params_str:
                for param in params_str.split(','):
                    param = param.strip()
                    if param:
                        # 简单的参数解析（可能不完美，但适用于大多数情况）
                        parts = param.split()
                        if len(parts) >= 2:
                            param_type = ' '.join(parts[:-1])
                            param_name = parts[-1]
                        else:
                            param_type = parts[0]
                            param_name = ""
                        params.append({
                            'type': param_type,
                            'name': param_name
                        })
            
            functions.append({
                'name': func_name,
                'return_type': return_type,
                'parameters': params
            })
        
        return functions
    
    def parse_header_file(self, file_path: Path) -> Dict:
        """解析头文件"""
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # 提取文件注释
        file_comment_match = re.search(r'/\*\*\s*\n\s*\*\s*@file\s+(\w+\.h)\s*\n\s*\*\s*@brief\s+(.+?)\s*\n', content, re.DOTALL)
        file_brief = file_comment_match.group(2).strip() if file_comment_match else ""
        
        # 提取函数
        functions = self.parse_function(content)
        
        # 提取宏定义（排除头文件保护宏）
        macros = []
        macro_pattern = r'#define\s+(\w+)(?:\([^)]*\))?\s+(.+?)(?=\n|$)'
        for match in re.finditer(macro_pattern, content, re.MULTILINE):
            macro_name = match.group(1)
            # 跳过头文件保护宏
            if macro_name.endswith('_H') or macro_name.endswith('_H_'):
                continue
            macro_value = match.group(2).strip()
            # 清理值（移除注释）
            if '//' in macro_value:
                macro_value = macro_value.split('//')[0].strip()
            macros.append({
                'name': macro_name,
                'value': macro_value
            })
        
        return {
            'file': file_path.name,
            'brief': file_brief,
            'functions': functions,
            'macros': macros
        }
    
    def generate_function_doc(self, func: Dict) -> str:
        """生成函数文档"""
        lines = []
        lines.append(f"### `{func['name']}`")
        lines.append("")
        
        # 函数签名
        params_str = ", ".join([f"{p['type']} {p['name']}" if p['name'] else p['type'] 
                               for p in func['parameters']])
        signature = f"```cpp\n{func['return_type']} {func['name']}({params_str});\n```"
        lines.append(signature)
        lines.append("")
        
        # 参数说明
        if func['parameters']:
            lines.append("**参数:**")
            lines.append("")
            for param in func['parameters']:
                if param['name']:
                    lines.append(f"- `{param['name']}` (`{param['type']}`): 参数说明")
                else:
                    lines.append(f"- `{param['type']}`: 参数说明")
            lines.append("")
        
        # 返回值说明
        if func['return_type'] != 'void':
            lines.append(f"**返回值:** `{func['return_type']}` - 返回值说明")
            lines.append("")
        else:
            lines.append("**返回值:** 无")
            lines.append("")
        
        # 示例（占位符）
        lines.append("**示例:**")
        lines.append("```cpp")
        params_example = ", ".join([p['name'] if p['name'] else "arg" 
                                   for p in func['parameters']])
        if func['return_type'] != 'void':
            lines.append(f"auto result = {func['name']}({params_example});")
        else:
            lines.append(f"{func['name']}({params_example});")
        lines.append("```")
        lines.append("")
        
        return "\n".join(lines)
    
    def generate_macro_doc(self, macro: Dict) -> str:
        """生成宏定义文档"""
        lines = []
        lines.append(f"### `{macro['name']}`")
        lines.append("")
        lines.append(f"```cpp")
        lines.append(f"#define {macro['name']} {macro['value']}")
        lines.append(f"```")
        lines.append("")
        lines.append("**说明:** 宏定义说明")
        lines.append("")
        return "\n".join(lines)
    
    def generate_file_doc(self, file_info: Dict) -> str:
        """生成单个头文件的文档"""
        lines = []
        lines.append(f"# {file_info['file']}")
        lines.append("")
        
        if file_info['brief']:
            lines.append(file_info['brief'])
            lines.append("")
        
        # 函数文档
        if file_info['functions']:
            lines.append("## 函数")
            lines.append("")
            for func in file_info['functions']:
                lines.append(self.generate_function_doc(func))
                lines.append("---")
                lines.append("")
        
        # 宏定义文档
        if file_info['macros']:
            lines.append("## 宏定义")
            lines.append("")
            for macro in file_info['macros']:
                lines.append(self.generate_macro_doc(macro))
                lines.append("---")
                lines.append("")
        
        return "\n".join(lines)
    
    def generate_index_doc(self, all_files: List[Dict]) -> str:
        """生成索引文档"""
        lines = []
        lines.append("# MQTT客户端库 API 文档")
        lines.append("")
        lines.append(f"**版本:** {self.version}")
        lines.append(f"**生成时间:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        lines.append("")
        lines.append("## 概述")
        lines.append("")
        lines.append("本文档描述了MQTT客户端库的公共API接口。")
        lines.append("")
        lines.append("## 头文件")
        lines.append("")
        
        for file_info in all_files:
            file_name = file_info['file']
            file_link = file_name.replace('.h', '.md')
            lines.append(f"- [{file_name}]({file_link})")
            if file_info['brief']:
                lines.append(f"  - {file_info['brief']}")
            lines.append("")
        
        lines.append("## 快速开始")
        lines.append("")
        lines.append("### 包含头文件")
        lines.append("")
        lines.append("```cpp")
        for file_info in all_files:
            file_name = file_info['file']
            lines.append(f"#include \"mqtt_client/{file_name}\"")
        lines.append("```")
        lines.append("")
        lines.append("### 基本使用")
        lines.append("")
        lines.append("```cpp")
        lines.append("// 示例代码")
        if all_files:
            first_func = None
            for file_info in all_files:
                if file_info['functions']:
                    first_func = file_info['functions'][0]
                    break
            if first_func:
                params_example = ", ".join([p['name'] if p['name'] else "arg" 
                                           for p in first_func['parameters']])
                if first_func['return_type'] != 'void':
                    lines.append(f"auto result = {first_func['name']}({params_example});")
                else:
                    lines.append(f"{first_func['name']}({params_example});")
        lines.append("```")
        lines.append("")
        
        return "\n".join(lines)
    
    def generate(self):
        """生成所有文档"""
        # 确保输出目录存在
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        # 查找所有头文件
        header_files = list(self.include_dir.glob("*.h"))
        
        if not header_files:
            print(f"警告: 在 {self.include_dir} 中未找到头文件")
            return
        
        all_files = []
        
        # 解析每个头文件
        for header_file in header_files:
            print(f"解析: {header_file.name}")
            file_info = self.parse_header_file(header_file)
            all_files.append(file_info)
            
            # 生成单个文件的文档
            doc_content = self.generate_file_doc(file_info)
            output_file = self.output_dir / f"{header_file.stem}.md"
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(doc_content)
            print(f"生成: {output_file}")
        
        # 生成索引文档
        index_content = self.generate_index_doc(all_files)
        index_file = self.output_dir / "API.md"
        with open(index_file, 'w', encoding='utf-8') as f:
            f.write(index_content)
        print(f"生成: {index_file}")
        
        print(f"\n文档生成完成！输出目录: {self.output_dir}")


def main():
    parser = argparse.ArgumentParser(description='生成MQTT客户端库API文档')
    parser.add_argument('--source-dir', required=True, help='源代码目录')
    parser.add_argument('--output-dir', required=True, help='输出目录')
    parser.add_argument('--version', default='1.0.0', help='版本号')
    
    args = parser.parse_args()
    
    generator = APIDocGenerator(args.source_dir, args.output_dir, args.version)
    generator.generate()


if __name__ == '__main__':
    main()
