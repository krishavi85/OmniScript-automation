#pragma once

#include <JuceHeader.h>

namespace omnistem::desktop {

struct WorkerResponse {
    bool ok{};
    juce::String message;
    juce::var payload;
};

class WorkerBridge final {
public:
    static WorkerResponse run(const juce::var& request, int timeoutMilliseconds = 3600000) {
        const auto script = locateRequestScript();
        if (!script.existsAsFile()) {
            return {false,
                    "Worker request script not found. Set OMNISTEM_WORKER_SCRIPT to request_once.py.",
                    {}};
        }

        const auto requestFile = juce::File::createTempFile(".omnistem-request.json");
        if (!requestFile.replaceWithText(juce::JSON::toString(request, false))) {
            return {false, "Could not create the worker request file.", {}};
        }

        juce::StringArray arguments;
        arguments.add(juce::SystemStats::getEnvironmentVariable("OMNISTEM_PYTHON", "python"));
        arguments.add(script.getFullPathName());
        arguments.add(requestFile.getFullPathName());

        juce::ChildProcess process;
        if (!process.start(arguments, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
            requestFile.deleteFile();
            return {false, "Could not start the OmniStem Python worker.", {}};
        }

        const auto completed = process.waitForProcessToFinish(timeoutMilliseconds);
        if (!completed) {
            process.kill();
            requestFile.deleteFile();
            return {false, "The worker request timed out.", {}};
        }

        const auto output = process.readAllProcessOutput().trim();
        requestFile.deleteFile();
        if (output.isEmpty())
            return {false, "The worker returned no response.", {}};

        const auto parsed = juce::JSON::parse(output);
        if (parsed.isVoid() || parsed.isUndefined())
            return {false, "The worker returned invalid JSON: " + output, {}};

        if (auto* root = parsed.getDynamicObject()) {
            const auto errorValue = root->getProperty("error");
            if (auto* error = errorValue.getDynamicObject()) {
                return {false, error->getProperty("message").toString(), parsed};
            }

            const auto jobValue = root->getProperty("job");
            if (auto* job = jobValue.getDynamicObject()) {
                const auto state = job->getProperty("state").toString();
                if (state == "failed" || state == "cancelled") {
                    const auto jobErrorValue = job->getProperty("error");
                    if (auto* jobError = jobErrorValue.getDynamicObject())
                        return {false, jobError->getProperty("message").toString(), parsed};
                    return {false, "Worker job " + state + ".", parsed};
                }
            }
        }

        return {true, "Worker operation completed.", parsed};
    }

    static juce::File locateRequestScript() {
        const auto configured = juce::SystemStats::getEnvironmentVariable("OMNISTEM_WORKER_SCRIPT", {});
        if (configured.isNotEmpty())
            return juce::File(configured);

        const auto relative = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("services")
                                  .getChildFile("ai-worker")
                                  .getChildFile("request_once.py");
        if (relative.existsAsFile())
            return relative;

        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        const auto bundled = executable.getParentDirectory()
                                .getChildFile("worker")
                                .getChildFile("request_once.py");
        return bundled;
    }
};

} // namespace omnistem::desktop
