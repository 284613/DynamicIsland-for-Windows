json_str = '{"code":"200","hourly":[{"fxTime":"2021-02-16T15:00+08:00","temp":"15","icon":"100"},{"fxTime":"2021-02-16T16:00+08:00","temp":"16","icon":"101"}]}'

def extract_array_field(json, arrayName, index, field):
    arrayPattern = '"' + arrayName + '":[{'
    startPos = json.find(arrayPattern)
    if startPos == -1:
        arrayPattern = '"' + arrayName + '":['
        startPos = json.find(arrayPattern)
        if startPos == -1: return ""
        startPos = json.find('{', startPos)
        if startPos == -1: return ""
        startPos -= 1

    objStart = startPos + len(arrayPattern) - 1
    if json[objStart] != '{':
        objStart = json.find('{', objStart)
        if objStart == -1: return ""

    for i in range(index):
        objStart = json.find('},{', objStart)
        if objStart == -1:
            objStart = json.find('}, {', objStart)
            if objStart == -1: return ""
            objStart += 3
        else:
            objStart += 2

    objEnd = json.find('}', objStart)
    if objEnd == -1: return ""
    
    objStr = json[objStart:objEnd+1]
    return objStr

print(extract_array_field(json_str, "hourly", 0, "temp"))
print(extract_array_field(json_str, "hourly", 1, "temp"))
