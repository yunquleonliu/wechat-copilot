import fs from "node:fs";
export function loadJson(filePath, fallback) {
    try {
        if (fs.existsSync(filePath)) {
            return JSON.parse(fs.readFileSync(filePath, "utf-8"));
        }
    }
    catch {
        // corrupted file — return fallback
    }
    return fallback;
}
export function saveJson(filePath, data) {
    const dir = filePath.substring(0, filePath.lastIndexOf("/"));
    if (dir)
        fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(filePath, JSON.stringify(data, null, 2), "utf-8");
}
