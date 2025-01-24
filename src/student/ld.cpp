#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#include "fle.hpp"
#include "string_utils.hpp"

FLEObject FLE_ld(const std::vector<FLEObject>& input_objects) {
    // 1. 合并段并收集符号
    //    - 遍历每个输入对象的段
    //    - 按段名合并并计算各段的偏移量
    //    - 设置段的读/写/执行属性
    auto local_symbol_sets = std::vector<std::set<std::string>>();  // 存储每个目标文件的局部符号
    for (const auto& obj : input_objects) {
        local_symbol_sets.push_back(std::set<std::string>());
        for (const auto& symbol : obj.symbols) {
            if (symbol.type == SymbolType::LOCAL)
                local_symbol_sets.back().insert(symbol.name);
        }
    }

    // 创建目标 FLEObject
    auto linked_object = FLEObject();
    auto global_symbols = std::map<std::string, Symbol>();
    auto segment_names = std::vector<std::string>{".text", ".rodata", ".data", ".bss"};
    auto segment_flags = std::vector<uint32_t>{5, 4, 6, 6};  // 设置段的权限标志
    auto segment_vaddr = std::vector<size_t>(4, 0);  // 各段的虚拟地址
    size_t virtual_address = 0x400000;  // 初始化虚拟内存基址

    // 所有段并合并
    for (int i = 0; i < 4; i++) {
        auto current_segment_name = segment_names[i];
        FLESection current_merged_section;
        current_merged_section.has_symbols = false;
        size_t section_offset = 0;  // 当前段内的偏移量
        segment_vaddr[i] = virtual_address;
        
        // 遍历每个输入文件
        for (size_t j = 0; j < input_objects.size(); j++) {
            auto current_object = input_objects[j];
            for (const auto& section_with_name : current_object.sections) {
                auto current_subsection_name = section_with_name.first;
                if (!starts_with(current_subsection_name, current_segment_name))  // 处理类似 .rodata.str1.1 的情况
                    continue;

                auto current_section = section_with_name.second;
                size_t section_size = 0;

                // 获取当前段的大小
                for (const auto& section_header : current_object.shdrs) {
                    if (section_header.name == current_subsection_name) {
                        section_size = section_header.size;
                        break;
                    }
                }

                // 将当前段的数据复制到合并段
                current_merged_section.data.insert(current_merged_section.data.end(), current_section.data.begin(), current_section.data.end());

                // 合并符号表
                for (auto symbol : current_object.symbols) {
                    if (symbol.section != current_subsection_name)
                        continue;
                    if (symbol.type == SymbolType::LOCAL)  // 对本地符号进行重命名
                        symbol.name = symbol.name + "@" + current_object.name;
                    symbol.offset += virtual_address;

                    auto existing_symbol = global_symbols.find(symbol.name);
                    if (existing_symbol == global_symbols.end() ||
                        (existing_symbol->second.type == SymbolType::UNDEFINED && (symbol.type == SymbolType::GLOBAL || symbol.type == SymbolType::WEAK)) ||
                        (existing_symbol->second.type == SymbolType::WEAK && symbol.type == SymbolType::GLOBAL)) {
                        global_symbols[symbol.name] = symbol;
                    }

                    if (existing_symbol != global_symbols.end() && 
                        existing_symbol->second.type == SymbolType::GLOBAL && 
                        symbol.type == SymbolType::GLOBAL) {
                        throw std::runtime_error("Multiple definitions of strong symbol: " + symbol.name);
                    }
                    current_merged_section.has_symbols = true;
                }

                // 合并重定位项
                for (auto reloc : current_section.relocs) {
                    reloc.offset += section_offset;
                    if (local_symbol_sets[j].count(reloc.symbol) == 1)  // 对本地符号重命名
                        reloc.symbol = reloc.symbol + "@" + current_object.name;
                    current_merged_section.relocs.push_back(reloc);
                }

                // 更新当前段偏移量和虚拟地址
                section_offset += section_size;
                virtual_address += section_size;
            }
        }

        // 更新最终的可执行文件中的段
        linked_object.sections[current_segment_name] = current_merged_section;
        ProgramHeader header;
        header.name = current_segment_name;
        header.vaddr = segment_vaddr[i];
        header.size = section_offset;
        header.flags = segment_flags[i];
        linked_object.phdrs.push_back(header);

        // 对齐虚拟地址
        virtual_address = (virtual_address + 0xfff) & ~0xfff;
    }

    // 3. 处理重定位
    for (int i = 0; i < 4; i++) {
        auto section_name = segment_names[i];
        auto& section = linked_object.sections[section_name];
        for (const auto& reloc : section.relocs) {
            if (global_symbols.count(reloc.symbol) == 0) {
                auto symbol_name = reloc.symbol;
                symbol_name = symbol_name.substr(0, symbol_name.find('@'));  // 重命名回本地符号
                throw std::runtime_error("Undefined symbol: " + symbol_name);
            }

            auto symbol = global_symbols.at(reloc.symbol);
            assert(symbol.type != SymbolType::UNDEFINED);
            int size = reloc.type == RelocationType::R_X86_64_64 ? 8 : 4;

            symbol.offset -= reloc.type == RelocationType::R_X86_64_PC32 ? segment_vaddr[i] + reloc.offset : 0;
            symbol.offset += reloc.addend;

            // 写入重定位值（按小端序存储）
            for (int j = 0; j < size; j++) {
                section.data[reloc.offset + j] = symbol.offset & 0xff;
                symbol.offset >>= 8;
            }
        }

        // 清空当前段的重定位项
        section.relocs.clear();
    }

    // 4. 设置程序入口点并返回可执行文件
    linked_object.type = ".exe";
    for (const auto& symbol : global_symbols) {
        if (symbol.second.name == "_start") {
            linked_object.entry = symbol.second.offset;
            break;
        }
    }

    return linked_object;
}
