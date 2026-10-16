//
// Created by raul on 10/14/26.
//

#ifndef DASHBOARD_H
#define DASHBOARD_H

static const char DASHBOARD_HTML[] =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"UTF-8\">\n"
"  <title>Hermes — Device Monitor</title>\n"
"  <style>\n"
"    body { font-family: sans-serif; background: #111; color: #eee; padding: 2rem; }\n"
"    h1   { color: #4fc3f7; }\n"
"    .card { background: #1e1e1e; border-radius: 8px; padding: 1rem; margin: 0.5rem 0; }\n"
"    .label { color: #888; font-size: 0.85rem; }\n"
"    .value { font-size: 1.4rem; font-weight: bold; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <h1>Hermes Device Monitor</h1>\n"
"  <div class=\"card\">\n"
"    <div class=\"label\">CPU Temperature</div>\n"
"    <div class=\"value\" id=\"cpu-temp\">--</div>\n"
"  </div>\n"
"  <div class=\"card\">\n"
"    <div class=\"label\">Memory Used</div>\n"
"    <div class=\"value\" id=\"memory\">--</div>\n"
"  </div>\n"
"  <div class=\"card\">\n"
"    <div class=\"label\">Disk Used</div>\n"
"    <div class=\"value\" id=\"disk\">--</div>\n"
"  </div>\n"
"  <div class=\"card\">\n"
"    <div class=\"label\">Uptime</div>\n"
"    <div class=\"value\" id=\"uptime\">--</div>\n"
"  </div>\n"
"  <div class=\"card\">\n"
"    <div class=\"label\">Load Average (1m)</div>\n"
"    <div class=\"value\" id=\"load\">--</div>\n"
"  </div>\n"
"  <p style=\"color:#555; font-size:0.8rem;\">Data not yet connected — coming in Step 6</p>\n"
"</body>\n"
"</html>\n";

#endif //DASHBOARD_H
