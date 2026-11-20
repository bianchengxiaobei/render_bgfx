#include "hbg/dawn/dawn_agent_client.h"

#include "Agent.grpc.pb.h"
#include "ClientAgent.grpc.pb.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <grpcpp/grpcpp.h>

#ifdef GetMessage
#undef GetMessage
#endif

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hbg::dawn {
namespace {

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;

const FieldDescriptor* field(const Message& message, const char* name) {
    const FieldDescriptor* descriptor = message.GetDescriptor()->FindFieldByName(name);
    if (descriptor == nullptr) {
        throw std::runtime_error(std::string("missing protobuf field: ") + name);
    }
    return descriptor;
}

std::string getString(const Message& message, const char* name) {
    return message.GetReflection()->GetString(message, field(message, name));
}

void setString(Message* message, const char* name, const std::string& value) {
    message->GetReflection()->SetString(message, field(*message, name), value);
}

void addString(Message* message, const char* name, const std::string& value) {
    message->GetReflection()->AddString(message, field(*message, name), value);
}

void setInt32(Message* message, const char* name, int32_t value) {
    message->GetReflection()->SetInt32(message, field(*message, name), value);
}

void setInt64(Message* message, const char* name, int64_t value) {
    message->GetReflection()->SetInt64(message, field(*message, name), value);
}

void setUInt64(Message* message, const char* name, uint64_t value) {
    message->GetReflection()->SetUInt64(message, field(*message, name), value);
}

void setBool(Message* message, const char* name, bool value) {
    message->GetReflection()->SetBool(message, field(*message, name), value);
}

void setEnum(Message* message, const char* name, int value) {
    message->GetReflection()->SetEnumValue(message, field(*message, name), value);
}

int getEnum(const Message& message, const char* name) {
    return message.GetReflection()->GetEnumValue(message, field(message, name));
}

Message* mutableMessage(Message* message, const char* name) {
    return message->GetReflection()->MutableMessage(message, field(*message, name));
}

const Message& repeatedMessage(const Message& message, const char* name, int index) {
    return message.GetReflection()->GetRepeatedMessage(message, field(message, name), index);
}

int repeatedSize(const Message& message, const char* name) {
    return message.GetReflection()->FieldSize(message, field(message, name));
}

bool hasField(const Message& message, const char* name) {
    return message.GetReflection()->HasField(message, field(message, name));
}

std::string normalizeAddress(std::string address) {
    constexpr const char* http = "http://";
    constexpr const char* https = "https://";
    if (address.rfind(http, 0) == 0) {
        address.erase(0, std::char_traits<char>::length(http));
    } else if (address.rfind(https, 0) == 0) {
        address.erase(0, std::char_traits<char>::length(https));
    }
    return address;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

void setDeadline(grpc::ClientContext& context, std::chrono::milliseconds timeout) {
    const auto duration = timeout.count() > 0 ? timeout : std::chrono::minutes(10);
    context.set_deadline(std::chrono::system_clock::now() + duration);
}

} // namespace

struct AgentClient::Impl {
    std::string address;
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<DAWNProto::ClientAgentService::Stub> client;
};

AgentClient::AgentClient(std::string address) : m_impl(std::make_unique<Impl>()) {
    m_impl->address = normalizeAddress(std::move(address));
    m_impl->channel = grpc::CreateChannel(m_impl->address, grpc::InsecureChannelCredentials());
    m_impl->client = DAWNProto::ClientAgentService::NewStub(m_impl->channel);
}

AgentClient::~AgentClient() = default;

AgentJobResult AgentClient::runJob(const AgentJobSpec& spec, std::chrono::milliseconds timeout,
                                   const std::function<void(std::size_t, std::size_t)>& onProgress) {
    AgentJobResult result;
    result.totalTasks = spec.tasks.size();

    DAWNProto::BeginMissionReq beginMission;
    setString(&beginMission, "missionID", spec.missionId);
    setString(&beginMission, "categoryID", "DAWN");
    setString(&beginMission, "customInfo", "hbg_dawnbake");
    setInt32(&beginMission, "gpuCount", 1);
    setBool(&beginMission, "IsPreview", false);
    Message* scheme = mutableMessage(&beginMission, "calculationSchemeInfo");
    setEnum(scheme, "scheme", 2);
    setString(scheme, "localAgentName", "");
    setString(scheme, "localExecutablePath", pathToUtf8(spec.localExecutablePath));

    grpc::ClientContext beginContext;
    setDeadline(beginContext, timeout);
    DAWNProto::GeneralAck beginAck;
    grpc::Status beginStatus = m_impl->client->BeginMission(&beginContext, beginMission, &beginAck);
    if (!beginStatus.ok()) {
        result.error = DawnBakeError::HiveConnectionLost;
        result.message = beginStatus.error_message();
        return result;
    }
    result.ackCode = getEnum(beginAck, "code");
    if (result.ackCode != 0) {
        result.error = DawnBakeError::HiveStartupFailure;
        result.message = getString(beginAck, "message");
        return result;
    }

    DAWNProto::RunJobToClientAgentReq runJob;
    setString(&runJob, "missionID", spec.missionId);
    setInt32(&runJob, "desiredNumAgents", 1);
    Message* full = mutableMessage(&runJob, "jobFullDesc");
    Message* basic = mutableMessage(full, "basicDesc");
    setString(basic, "categoryID", "DAWN");
    setString(basic, "jobID", spec.jobId);
    setString(basic, "jobDir", pathToUtf8(spec.jobDirectory));
    setString(basic, "exeDir", spec.exeDirectory);
    setString(basic, "fileCacheDir",
              spec.fileCacheDirectory.empty() ? pathToUtf8(spec.jobDirectory) : spec.fileCacheDirectory);
    setString(basic, "parameters", spec.parameters.empty() ? spec.jobId : spec.parameters);
    setString(basic, "exeTag", spec.exeTag);
    setInt32(basic, "exeVersionMain", spec.exeVersionMain);
    setInt32(basic, "exeVersionSub", spec.exeVersionSub);
    setBool(basic, "isManualDebug", false);
    int32_t totalCost = 0;
    for (const std::string& file : spec.jobFiles) {
        addString(basic, "jobFiles", file);
    }
    for (const std::string& file : spec.exeFiles) {
        addString(basic, "exeFiles", file);
    }
    for (const AgentTaskSpec& task : spec.tasks) {
        Message* taskDesc = full->GetReflection()->AddMessage(full, field(*full, "taskDescList"));
        setString(taskDesc, "taskID", task.taskId);
        setString(taskDesc, "parameters", task.parameters);
        setInt32(taskDesc, "cost", task.cost);
        for (const std::string& file : task.resultFiles) {
            addString(taskDesc, "resultFilesMust", file);
        }
        totalCost += task.cost;
    }
    setInt64(basic, "totalTaskCost", totalCost);

    grpc::ClientContext runContext;
    setDeadline(runContext, timeout);
    DAWNProto::GeneralAck runAck;
    grpc::Status runStatus = m_impl->client->RunJob(&runContext, runJob, &runAck);
    if (!runStatus.ok()) {
        result.error = DawnBakeError::HiveConnectionLost;
        result.message = runStatus.error_message();
        return result;
    }
    result.ackCode = getEnum(runAck, "code");
    if (result.ackCode != 0) {
        result.error = DawnBakeError::JobStartFailure;
        result.message = getString(runAck, "message");
        return result;
    }

    DAWNProto::GetJobResultReq resultRequest;
    setString(&resultRequest, "jobID", spec.jobId);
    grpc::ClientContext resultContext;
    setDeadline(resultContext, timeout);
    std::unique_ptr<grpc::ClientReader<DAWNProto::TaskResult>> reader(
        m_impl->client->GetJobResult(&resultContext, resultRequest));
    DAWNProto::TaskResult taskResult;
    bool finished = false;
    while (reader->Read(&taskResult)) {
        if (hasField(taskResult, "errMsg")) {
            const Message& errorMessage =
                taskResult.GetReflection()->GetMessage(taskResult, field(taskResult, "errMsg"));
            const std::string error = getString(errorMessage, "value");
            if (error == "D.O.N.E") {
                finished = true;
                resultContext.TryCancel();
                break;
            }
            result.error = DawnBakeError::JobFailure;
            result.message = error.empty() ? "Dawn task failed" : error;
            break;
        }
        ++result.completedTasks;
        if (onProgress) {
            onProgress(result.completedTasks, result.totalTasks);
        }
    }
    const grpc::Status resultStatus = reader->Finish();
    if (result.error == DawnBakeError::None && !resultStatus.ok() && !finished) {
        result.error = DawnBakeError::HiveConnectionLost;
        result.message = resultStatus.error_message();
    } else if (result.error == DawnBakeError::None && !finished) {
        result.error = DawnBakeError::JobFailure;
        result.message = "result stream ended without D.O.N.E";
    }

    DAWNProto::EndMissionReq endMission;
    setString(&endMission, "missionID", spec.missionId);
    grpc::ClientContext endContext;
    setDeadline(endContext, timeout);
    DAWNProto::GeneralAck endAck;
    const grpc::Status endStatus = m_impl->client->EndMission(&endContext, endMission, &endAck);
    if (result.error == DawnBakeError::None && (!endStatus.ok() || getEnum(endAck, "code") != 0)) {
        result.error = DawnBakeError::HiveConnectionLost;
        result.message = endStatus.ok() ? getString(endAck, "message") : endStatus.error_message();
    }
    return result;
}

bool AgentClient::uploadFile(const std::string& agentAddress, const std::string& jobId,
                             const std::filesystem::path& localPath, const std::string& remotePath,
                             std::chrono::milliseconds timeout, std::string* error) {
    std::ifstream input(localPath, std::ios::binary | std::ios::ate);
    if (!input) {
        if (error != nullptr) {
            *error = "cannot open upload source: " + pathToUtf8(localPath);
        }
        return false;
    }
    const std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0) {
        if (error != nullptr) {
            *error = "cannot determine upload size";
        }
        return false;
    }

    const std::string address = normalizeAddress(agentAddress);
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto agent = DAWNProto::AgentService::NewStub(channel);
    grpc::ClientContext context;
    setDeadline(context, timeout);
    auto writer = agent->UploadFile(&context);

    DAWNProto::UploadFileReq header;
    setString(&header, "jobID", jobId);
    setString(&header, "filePath", remotePath);
    setUInt64(&header, "fileSize", static_cast<uint64_t>(size));
    if (!writer->Write(header)) {
        if (error != nullptr) {
            *error = "upload stream rejected header";
        }
        writer->WritesDone();
        writer->Finish();
        return false;
    }

    std::vector<char> buffer(64u * 1024u);
    bool ok = true;
    if (size == 0) {
        DAWNProto::UploadFileReq chunk;
        setString(&chunk, "fileData", "");
        setBool(&chunk, "fileDataDone", true);
        ok = writer->Write(chunk);
    }
    while (ok && input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) {
            break;
        }
        DAWNProto::UploadFileReq chunk;
        setString(&chunk, "fileData", std::string(buffer.data(), static_cast<std::size_t>(count)));
        setBool(&chunk, "fileDataDone", input.peek() == std::char_traits<char>::eof());
        if (!writer->Write(chunk)) {
            ok = false;
            break;
        }
    }
    writer->WritesDone();

    DAWNProto::UploadFileAck ack;
    while (writer->Read(&ack)) {
        const int type = getEnum(ack, "type");
        if (type == 3) {
            ok = false;
            if (error != nullptr) {
                *error = hasField(ack, "message") ? getString(ack, "message") : "upload failed";
            }
        } else if (type == 2) {
            ok = false;
            if (error != nullptr) {
                *error = "file is not in server cache";
            }
        }
    }
    const grpc::Status status = writer->Finish();
    if (!status.ok()) {
        ok = false;
        if (error != nullptr) {
            *error = status.error_message();
        }
    }
    return ok;
}

} // namespace hbg::dawn
