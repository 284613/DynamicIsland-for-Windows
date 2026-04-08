#include <iostream>
#include <string>

std::string ExtractJsonField(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    
    if (pos < json.length() && json[pos] == '\"') {
        pos++;
        std::string val;
        while (pos < json.length() && json[pos] != '\"') {
            if (json[pos] == '\\' && pos + 1 < json.length()) {
                pos++;
                val += json[pos++];
            } else {
                val += json[pos++];
            }
        }
        return val;
    }

    std::string val;
    while (pos < json.length() && json[pos] != ',' && json[pos] != '}') {
        val += json[pos++];
    }
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
    return val;
}

std::string ExtractJsonArrayField(const std::string& json, const char* arrayName, int index, const char* field) {
    std::string arrayPattern = "\"" + std::string(arrayName) + "\":[{";
    size_t startPos = json.find(arrayPattern);
    if (startPos == std::string::npos) {
        arrayPattern = "\"" + std::string(arrayName) + "\":[";
        startPos = json.find(arrayPattern);
        if (startPos == std::string::npos) return "";
        startPos = json.find("{", startPos);
        if (startPos == std::string::npos) return "";
        startPos -= 1;
    }
    
    size_t objStart = startPos + arrayPattern.length() - 1;
    if (json[objStart] != '{') {
        objStart = json.find("{", objStart);
        if (objStart == std::string::npos) return "";
    }

    for (int i = 0; i < index; ++i) {
        objStart = json.find("},{", objStart);
        if (objStart == std::string::npos) {
            objStart = json.find("}, {", objStart);
            if (objStart == std::string::npos) return "";
            objStart += 3;
        } else {
            objStart += 2;
        }
    }
    
    size_t objEnd = json.find("}", objStart);
    if (objEnd == std::string::npos) return "";
    
    std::string objStr = json.substr(objStart, objEnd - objStart + 1);
    return ExtractJsonField(objStr, field);
}

int main() {
    std::string json = "{\"code\":\"200\",\"hourly\":[{\"fxTime\":\"2021-02-16T15:00+08:00\",\"temp\":\"15\",\"icon\":\"100\"},{\"fxTime\":\"2021-02-16T16:00+08:00\",\"temp\":\"16\",\"icon\":\"101\"}]}";
    std::cout << ExtractJsonArrayField(json, "hourly", 0, "temp") << std::endl;
    std::cout << ExtractJsonArrayField(json, "hourly", 1, "temp") << std::endl;
    return 0;
}
