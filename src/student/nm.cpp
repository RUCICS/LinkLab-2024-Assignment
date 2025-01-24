#include "fle.hpp"
#include <iomanip>
#include <iostream>

void FLE_nm(const FLEObject& obj)
{
    // 遍历符号表
    for (const auto& symbol : obj.symbols) {
        //  跳过未定义符号
        if (symbol.section.empty()) {
            continue;  // 符号没有 section，表示是未定义符号
        }

        // 确定符号类型和输出格式
        std::string type;
        if (symbol.type == SymbolType::WEAK) {
            type = (symbol.section == ".text") ? "W" : "V";
        } else if (symbol.type == SymbolType::GLOBAL) {
            if (symbol.section == ".text") {
                type = "T"; // 全局函数符号
            } else if (symbol.section == ".data") {
                type = "D"; // 全局数据符号
            } else if (symbol.section == ".bss") {
                type = "B"; // 全局未初始化数据符号
            }
        } else if (symbol.type == SymbolType::LOCAL) {
            if (symbol.section == ".text") {
                type = "t"; // 局部函数符号
            } else if (symbol.section == ".data") {
                type = "d"; // 局部数据符号
            } else if (symbol.section == ".bss") {
                type = "b"; // 局部未初始化数据符号
            }
        }

        // 输出符号信息
        // 确保地址是16位十六进制，左侧补零
        std::cout << std::setw(16) << std::setfill('0') << std::hex << symbol.offset << " "
                  << type << " "
                  << symbol.name << std::endl;
    }
}
