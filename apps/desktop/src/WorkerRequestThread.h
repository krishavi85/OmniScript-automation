#pragma once

#include "WorkerApiClient.h"

#include <JuceHeader.h>

namespace omnistem::desktop {

class WorkerRequestThread final : public juce::Thread {
public:
    enum class Operation {
        health,
        methods,
        engines,
        models,
        rpc,
        jobStatus,
        cancelJob,
    };

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void workerRequestFinished(Operation operation, ApiResult result) = 0;
    };

    WorkerRequestThread(WorkerApiClient apiClient,
                        Operation requestedOperation,
                        juce::String valueToUse,
                        juce::var parametersToUse,
                        Listener& resultListener)
        : juce::Thread("OmniStem worker request"),
          client(std::move(apiClient)),
          operation(requestedOperation),
          value(std::move(valueToUse)),
          parameters(std::move(parametersToUse)),
          listener(resultListener) {}

    ~WorkerRequestThread() override {
        signalThreadShouldExit();
        stopThread(5000);
    }

    void run() override {
        ApiResult result;
        switch (operation) {
            case Operation::health: result = client.health(); break;
            case Operation::methods: result = client.methods(); break;
            case Operation::engines: result = client.engines(); break;
            case Operation::models: result = client.models(); break;
            case Operation::rpc: result = client.rpc(value, parameters); break;
            case Operation::jobStatus: result = client.jobStatus(value); break;
            case Operation::cancelJob: result = client.cancelJob(value); break;
        }

        if (!threadShouldExit()) {
            auto safeListener = juce::WeakReference<Listener>(&listener);
            juce::MessageManager::callAsync(
                [safeListener, operationToReport = operation, result = std::move(result)]() mutable {
                    if (safeListener != nullptr)
                        safeListener->workerRequestFinished(operationToReport, std::move(result));
                });
        }
    }

private:
    WorkerApiClient client;
    Operation operation;
    juce::String value;
    juce::var parameters;
    Listener& listener;
};

} // namespace omnistem::desktop
