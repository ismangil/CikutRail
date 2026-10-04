// Config page on the home network (http://<node>.local), behind HTTP Basic
// login (user "admin", settings::adminPassword()). Every form carries a
// per-boot token, so another site open in the browser can't submit it
// with the saved login. Nothing here restarts the node.
#pragma once

namespace configpage {

void handleRoot();
void handleNetwork();
void handleNames();
void handleTurnout();
void handleBehaviour();
void handleAdmin();
void handleFactory();

}  // namespace configpage
