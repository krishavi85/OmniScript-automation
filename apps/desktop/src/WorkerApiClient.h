#pragma once

#include <JuceHeader.h>

namespace omnistem::desktop {

struct ApiResult {
    bool ok{};
    int statusCode{};
    juce::String message;
    juce::var payload;
};

class WorkerApiClient final {
public:
    explicit WorkerApiClient(juce::String endpoint = {})
        : baseUrl(resolveEndpoint(std::move(endpoint))) {}

    const juce::String& getBaseUrl() const noexcept { return baseUrl; }

    ApiResult health(int timeoutMs = 3000) const {
        return get("/health", timeoutMs);
    }

    ApiResult methods(int timeoutMs = 3000) const {
        return get("/methods", timeoutMs);
    }

    ApiResult engines(int timeoutMs = 3000) const {
        return get("/engines", timeoutMs);
    }

    ApiResult models(int timeoutMs = 5000) const {
        return get("/models?include_deprecated=false&sort_by=name", timeoutMs);
    }

    ApiResult rpc(const juce::String& method,
                  juce::var params,
                  int timeoutMs = 30000) const {
        auto request = std::make_unique<juce::DynamicObject>();
        request->setProperty("id", juce::Uuid().toString());
        request->setProperty("method", method);
        request->setProperty("params", std::move(params));
        return postJson("/rpc", juce::var(request.release()), timeoutMs);
    }

    ApiResult jobStatus(const juce::String& jobId, int timeoutMs = 5000) const {
        return get("/jobs/" + juce::URL::addEscapeChars(jobId, true), timeoutMs);
    }

    ApiResult cancelJob(const juce::String& jobId, int timeoutMs = 5000) const {
        return postJson("/jobs/" + juce::URL::addEscapeChars(jobId, true) + "/cancel",
                        juce::var(new juce::DynamicObject()),
                        timeoutMs);
    }

private:
    static juce::String resolveEndpoint(juce::String configured) {
        if (configured.isEmpty())
            configured = juce::SystemStats::getEnvironmentVariable(
                "OMNISTEM_API_URL", "http://127.0.0.1:8765");
        while (configured.endsWithChar('/'))
            configured = configured.dropLastCharacters(1);
        return configured;
    }

    ApiResult get(const juce::String& path, int timeoutMs) const {
        int status = 0;
        const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs(timeoutMs)
                                 .withNumRedirectsToFollow(0)
                                 .withStatusCode(&status)
                                 .withExtraHeaders("Accept: application/json\r\n");
        return readResponse(juce::URL(baseUrl + path).createInputStream(options), status);
    }

    ApiResult postJson(const juce::String& path,
                       const juce::var& body,
                       int timeoutMs) const {
        int status = 0;
        const auto json = juce::JSON::toString(body, false);
        const auto url = juce::URL(baseUrl + path).withPOSTData(json);
        const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                 .withConnectionTimeoutMs(timeoutMs)
                                 .withNumRedirectsToFollow(0)
                                 .withStatusCode(&status)
                                 .withHttpRequestCmd("POST")
                                 .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n");
        return readResponse(url.createInputStream(options), status);
    }

    static ApiResult readResponse(std::unique_ptr<juce::InputStream> stream, int status) {
        if (stream == nullptr)
            return {false, status, "Could not connect to the OmniStem worker API.", {}};

        const auto text = stream->readEntireStreamAsString();
        const auto parsed = juce::JSON::parse(text);
        if (parsed.isVoid() || parsed.isUndefined())
            return {false, status, "Worker API returned invalid JSON.", {}};

        if (status < 200 || status >= 300) {
            juce::String message = "Worker API request failed with HTTP " + juce::String(status);
            if (auto* object = parsed.getDynamicObject()) {
                const auto detail = object->getProperty("detail");
                if (!detail.isVoid())
                    message = juce::JSON::toString(detail, false);
            }
            return {false, status, message, parsed};
        }

        return {true, status, "OK", parsed};
    }

    juce::String baseUrl;
};

} // namespace omnistem::desktop
