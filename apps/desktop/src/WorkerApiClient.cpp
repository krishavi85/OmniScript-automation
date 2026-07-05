#include "WorkerApiClient.h"

namespace omnistem::desktop {

void verifyWorkerApiClientCompiles() {
    WorkerApiClient client;
    (void) client.getBaseUrl();
}

} // namespace omnistem::desktop

#include "BatchPanel.cpp"
#include "GodPanel.cpp"
