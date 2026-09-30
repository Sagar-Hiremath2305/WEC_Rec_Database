#pragma once
#include <vector>
#include <string>

// Process command and return a RESP formatted reply
std::string execute_command(const std::vector<std::string>& cmd);
