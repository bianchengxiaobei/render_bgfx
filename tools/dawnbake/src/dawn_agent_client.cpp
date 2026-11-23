#include "hbg/dawn/dawn_agent_client.h"

#include "Agent.grpc.pb.h"
#include "ClientAgent.grpc.pb.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <grpcpp/grpcpp.h>

#ifdef GetMessage
#undef GetMessage
#endif

#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

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

bool equalsIgnoreCase(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[i]);
        if (std::toupper(a) != std::toupper(b)) {
            return false;
        }
    }
    return true;
}

bool fileSha1Upper(const std::filesystem::path& path, std::string* hash, std::string* error) {
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    std::vector<unsigned char> hashObject;
    std::vector<unsigned char> digest(20);
    bool ok = false;
    auto fail = [&](const char* message) {
        if (error != nullptr) {
            *error = std::string(message) + ": " + pathToUtf8(path);
        }
        if (hashHandle != nullptr) {
            BCryptDestroyHash(hashHandle);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        return false;
    };

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
    if (status < 0) {
        return fail("cannot open SHA1 provider");
    }
    ULONG objectSize = 0;
    ULONG resultSize = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                               sizeof(objectSize), &resultSize, 0);
    if (status < 0) {
        return fail("cannot query SHA1 object size");
    }
    hashObject.resize(objectSize);
    status = BCryptCreateHash(algorithm, &hashHandle, hashObject.data(), objectSize, nullptr, 0, 0);
    if (status < 0) {
        hashHandle = nullptr;
        return fail("cannot create SHA1 hash");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("cannot open file for SHA1");
    }
    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            status = BCryptHashData(hashHandle, reinterpret_cast<PUCHAR>(buffer.data()),
                                    static_cast<ULONG>(count), 0);
            if (status < 0) {
                return fail("cannot hash file data");
            }
        }
    }
    if (!input.eof()) {
        return fail("cannot read file for SHA1");
    }
    status = BCryptFinishHash(hashHandle, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (status < 0) {
        return fail("cannot finish SHA1");
    }
    ok = true;
    static constexpr char hex[] = "0123456789ABCDEF";
    hash->clear();
    hash->reserve(digest.size() * 2u);
    for (unsigned char byte : digest) {
        hash->push_back(hex[byte >> 4u]);
        hash->push_back(hex[byte & 0x0fu]);
    }
    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
#else
    (void)path;
    (void)hash;
    if (error != nullptr) {
        *error = "SHA1 verification is unsupported on this platform";
    }
    return false;
#endif
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

    if (result.error == DawnBakeError::None && !spec.downloadDirectory.empty()) {
        std::vector<AgentDownloadFile> downloads;
        for (const AgentTaskSpec& task : spec.tasks) {
            for (const std::string& file : task.resultFiles) {
                const std::filesystem::path target = spec.downloadDirectory / file;
                if (!spec.forceDownloadResults && std::filesystem::is_regular_file(target)) {
                    continue;
                }
                std::filesystem::path remote(spec.exeDirectory);
                remote /= file;
                downloads.push_back(AgentDownloadFile{pathToUtf8(remote), target});
            }
        }
        std::string downloadError;
        if (!downloadFiles(spec.agentFileAddress, spec.jobId, downloads, timeout, &downloadError)) {
            result.error = DawnBakeError::ResultImportFailure;
            result.message = "failed to download results: " + downloadError;
        }
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

bool AgentClient::batchUploadFileInServerCache(const std::string& agentAddress, const std::string& jobId,
                                               const std::vector<AgentCacheFile>& files,
                                               std::chrono::milliseconds timeout, std::string* error) {
    if (files.empty()) {
        if (error != nullptr) {
            *error = "batch cache upload requires at least one file";
        }
        return false;
    }

    const std::string address = normalizeAddress(agentAddress);
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto agent = DAWNProto::AgentService::NewStub(channel);

    DAWNProto::BatchUploadFilesInServerCacheReq request;
    request.set_jobid(jobId);
    for (const AgentCacheFile& file : files) {
        DAWNProto::UploadFileInfo* info = request.add_files();
        info->set_hash(file.hash);
        info->set_filepath(file.remotePath);
    }

    grpc::ClientContext context;
    setDeadline(context, timeout);
    DAWNProto::BatchUploadFilesInServerCacheAck ack;
    const grpc::Status status = agent->BatchUploadFileInServerCache(&context, request, &ack);
    if (!status.ok()) {
        if (error != nullptr) {
            *error = status.error_message();
        }
        return false;
    }
    if (ack.acks_size() != static_cast<int>(files.size())) {
        if (error != nullptr) {
            *error = "batch cache upload ack count mismatch";
        }
        return false;
    }
    if (ack.has_message() && !ack.message().empty()) {
        if (error != nullptr) {
            *error = ack.message();
        }
        return false;
    }

    for (int i = 0; i < ack.acks_size(); ++i) {
        const DAWNProto::UploadFileAck& item = ack.acks(i);
        const DAWNProto::UploadFileAck_UploadFileAckType type = item.type();
        if (type == DAWNProto::UploadFileAck_UploadFileAckType_Done ||
            type == DAWNProto::UploadFileAck_UploadFileAckType_DoneInCache) {
            continue;
        }
        if (error != nullptr) {
            const std::string message = item.has_message() && !item.message().empty()
                                            ? item.message()
                                            : DAWNProto::UploadFileAck_UploadFileAckType_Name(type);
            *error = "batch cache upload failed for " + files[static_cast<std::size_t>(i)].remotePath + ": " + message;
        }
        return false;
    }
    return true;
}

bool AgentClient::uploadJobFilesToAnotherAgent(const std::string& agentAddress, const std::string& jobId,
                                               const std::string& anotherAgentAddress,
                                               const std::vector<std::string>& filePaths,
                                               std::chrono::milliseconds timeout, std::string* error) {
    if (filePaths.empty()) {
        if (error != nullptr) {
            *error = "job file transfer requires at least one file";
        }
        return false;
    }

    const std::string address = normalizeAddress(agentAddress);
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto agent = DAWNProto::AgentService::NewStub(channel);

    DAWNProto::UploadToAnotherAgentReq request;
    request.set_jobid(jobId);
    request.set_anotheragentaddress(anotherAgentAddress);
    for (const std::string& path : filePaths) {
        request.add_filepaths(path);
    }

    grpc::ClientContext context;
    setDeadline(context, timeout);
    DAWNProto::GeneralAck ack;
    const grpc::Status status = agent->UploadJobFilesToAnotherAgent(&context, request, &ack);
    if (!status.ok()) {
        if (error != nullptr) {
            *error = status.error_message();
        }
        return false;
    }
    const int code = getEnum(ack, "code");
    if (code != 0) {
        if (error != nullptr) {
            const std::string message = getString(ack, "message");
            *error = message.empty() ? "upload job files to another agent failed" : message;
        }
        return false;
    }
    return true;
}

bool AgentClient::uploadMultiFiles(const std::string& agentAddress, const std::string& jobId,
                                   const std::vector<AgentUploadFile>& files, std::chrono::milliseconds timeout,
                                   std::string* error) {
    if (files.empty()) {
        if (error != nullptr) {
            *error = "multi-file upload requires at least one file";
        }
        return false;
    }

    const std::string address = normalizeAddress(agentAddress);
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto agent = DAWNProto::AgentService::NewStub(channel);

    DAWNProto::UploadMultiFilesReq request;
    request.set_jobid(jobId);
    for (const AgentUploadFile& file : files) {
        DAWNProto::UploadFileInfo* info = request.add_files();
        info->set_hash(file.hash);
        info->set_filepath(file.remotePath);
        if (!file.data.empty()) {
            info->set_filedata(reinterpret_cast<const char*>(file.data.data()), file.data.size());
        }
    }

    grpc::ClientContext context;
    setDeadline(context, timeout);
    DAWNProto::UploadMultiFilesAck ack;
    const grpc::Status status = agent->UploadMultiFiles(&context, request, &ack);
    if (!status.ok()) {
        if (error != nullptr) {
            *error = status.error_message();
        }
        return false;
    }
    if (ack.acks_size() != static_cast<int>(files.size())) {
        if (error != nullptr) {
            *error = "multi-file upload ack count mismatch";
        }
        return false;
    }
    if (ack.has_message() && !ack.message().empty()) {
        if (error != nullptr) {
            *error = ack.message();
        }
        return false;
    }

    for (int i = 0; i < ack.acks_size(); ++i) {
        const DAWNProto::UploadFileAck& item = ack.acks(i);
        const DAWNProto::UploadFileAck_UploadFileAckType type = item.type();
        if (type == DAWNProto::UploadFileAck_UploadFileAckType_Done ||
            type == DAWNProto::UploadFileAck_UploadFileAckType_DoneInCache) {
            continue;
        }
        if (error != nullptr) {
            const std::string message = item.has_message() && !item.message().empty()
                                            ? item.message()
                                            : DAWNProto::UploadFileAck_UploadFileAckType_Name(type);
            *error = "multi-file upload failed for " + files[static_cast<std::size_t>(i)].remotePath + ": " + message;
        }
        return false;
    }
    return true;
}

bool AgentClient::downloadFile(const std::string& agentAddress, const std::string& jobId,
                               const std::string& remotePath, const std::filesystem::path& localPath,
                               std::chrono::milliseconds timeout, std::string* error) {
    const std::string address = normalizeAddress(agentAddress);
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto agent = DAWNProto::AgentService::NewStub(channel);

    DAWNProto::DownloadFileReq request;
    request.set_jobid(jobId);
    request.set_filepath(remotePath);

    grpc::ClientContext context;
    setDeadline(context, timeout);
    std::unique_ptr<grpc::ClientReader<DAWNProto::DownloadFileAck>> reader = agent->DownloadFile(&context, request);

    const std::filesystem::path temporary = localPath.string() + ".part";
    std::error_code ec;
    if (!localPath.parent_path().empty()) {
        std::filesystem::create_directories(localPath.parent_path(), ec);
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error != nullptr) {
            *error = "cannot open download target: " + pathToUtf8(temporary);
        }
        return false;
    }

    DAWNProto::DownloadFileAck ack;
    bool done = false;
    while (reader->Read(&ack)) {
        const DAWNProto::DownloadFileAck_DownloadFileAckType type = ack.type();
        if (type == DAWNProto::DownloadFileAck_DownloadFileAckType_Data ||
            type == DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone) {
            if (type == DAWNProto::DownloadFileAck_DownloadFileAckType_Data && !ack.has_filedata()) {
                if (error != nullptr) {
                    *error = "agent download data message is missing fileData for " + remotePath;
                }
                context.TryCancel();
                reader->Finish();
                output.close();
                std::filesystem::remove(temporary, ec);
                return false;
            }            if (ack.has_filedata() && !ack.filedata().empty()) {
                output.write(ack.filedata().data(), static_cast<std::streamsize>(ack.filedata().size()));
                if (!output) {
                    if (error != nullptr) {
                        *error = "cannot write download target: " + pathToUtf8(temporary);
                    }
                    context.TryCancel();
                    reader->Finish();
                    output.close();
                    std::filesystem::remove(temporary, ec);
                    return false;
                }
            }
            if (type == DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone) {
                done = true;
                break;
            }
            continue;
        }
        if (error != nullptr) {
            const std::string message = ack.has_message() && !ack.message().empty()
                                            ? ack.message()
                                            : DAWNProto::DownloadFileAck_DownloadFileAckType_Name(type);
            *error = "agent download failed for " + remotePath + ": " + message;
        }
        context.TryCancel();
        reader->Finish();
        output.close();
        std::filesystem::remove(temporary, ec);
        return false;
    }

    const grpc::Status status = reader->Finish();
    output.close();
    if (!done) {
        if (error != nullptr) {
            *error = status.ok() ? "download stream ended without DataDone" : status.error_message();
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }
    if (!status.ok()) {
        if (error != nullptr) {
            *error = status.error_message();
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }
    if (!ack.has_filehash() || ack.filehash().empty()) {
        if (error != nullptr) {
            *error = "download ack is missing fileHash for " + remotePath;
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::string actualHash;
    std::string hashError;
    if (!fileSha1Upper(temporary, &actualHash, &hashError)) {
        if (error != nullptr) {
            *error = hashError;
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }
    if (!equalsIgnoreCase(actualHash, ack.filehash())) {
        if (error != nullptr) {
            *error = "download fileHash mismatch for " + remotePath + ": expected " + ack.filehash() +
                     ", got " + actualHash;
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }

    std::filesystem::remove(localPath, ec);
    std::filesystem::rename(temporary, localPath, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "cannot commit download target: " + ec.message();
        }
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

bool AgentClient::downloadMultiFiles(const std::string& agentAddress, const std::string& jobId,
                                     const std::vector<AgentDownloadFile>& files,
                                     std::chrono::milliseconds timeout, std::string* error) {
    if (files.empty()) {
        if (error != nullptr) {
            *error = "multi-file download requires at least one file";
        }
        return false;
    }

    std::map<std::string, std::size_t> indexes;
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (files[i].remotePath.empty()) {
            if (error != nullptr) {
                *error = "multi-file download path is empty";
            }
            return false;
        }
        if (!indexes.emplace(files[i].remotePath, i).second) {
            if (error != nullptr) {
                *error = "multi-file download has duplicate path: " + files[i].remotePath;
            }
            return false;
        }
    }

    const std::string address = normalizeAddress(agentAddress);
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto agent = DAWNProto::AgentService::NewStub(channel);

    DAWNProto::DownloadMultiFilesReq request;
    request.set_jobid(jobId);
    for (const AgentDownloadFile& file : files) {
        request.add_filepathes(file.remotePath);
    }

    struct State {
        std::filesystem::path temporary;
        std::unique_ptr<std::ofstream> output;
        bool done = false;
        bool started = false;
    };
    std::vector<State> states(files.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::error_code ec;
        states[i].temporary = files[i].localPath.string() + ".part";
        if (!files[i].localPath.parent_path().empty()) {
            std::filesystem::create_directories(files[i].localPath.parent_path(), ec);
        }
    }

    grpc::ClientContext context;
    setDeadline(context, timeout);
    std::unique_ptr<grpc::ClientReader<DAWNProto::DownloadMultiFilesAck>> reader =
        agent->DownloadMultiFiles(&context, request);

    int current = -1;
    auto cleanup = [&]() {
        for (std::size_t i = 0; i < states.size(); ++i) {
            if (states[i].output) {
                states[i].output->close();
                states[i].output.reset();
            }
            if (!states[i].done) {
                std::error_code ec;
                std::filesystem::remove(states[i].temporary, ec);
            }
        }
        current = -1;
    };
    auto fail = [&](const std::string& message) {
        context.TryCancel();
        reader->Finish();
        cleanup();
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    DAWNProto::DownloadMultiFilesAck ack;
    while (reader->Read(&ack)) {
        if (!ack.errmsg().empty()) {
            return fail("agent multi-file download failed: " + ack.errmsg());
        }
        for (const DAWNProto::DownloadFileAck& item : ack.items()) {
            const DAWNProto::DownloadFileAck_DownloadFileAckType type = item.type();
            if (type == DAWNProto::DownloadFileAck_DownloadFileAckType_Fail ||
                type == DAWNProto::DownloadFileAck_DownloadFileAckType_Fail_FileNotFound) {
                const std::string message = item.has_message() && !item.message().empty()
                                                ? item.message()
                                                : DAWNProto::DownloadFileAck_DownloadFileAckType_Name(type);
                return fail("agent multi-file download failed for " + item.filename() + ": " + message);
            }
            if (type != DAWNProto::DownloadFileAck_DownloadFileAckType_Data &&
                type != DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone) {
                return fail("agent multi-file download returned unknown type " + std::to_string(type) +
                            " for " + item.filename());
            }
            if (item.filename().empty()) {
                return fail("agent multi-file download returned an item without filename");
            }
            const auto found = indexes.find(item.filename());
            if (found == indexes.end()) {
                return fail("agent multi-file download returned unexpected file: " + item.filename());
            }
            const std::size_t index = found->second;
            if (states[index].done) {
                return fail("agent multi-file download returned duplicate file: " + item.filename());
            }
            if (current >= 0 && current != static_cast<int>(index)) {
                return fail("agent multi-file download interleaved files: current " + files[static_cast<std::size_t>(current)].remotePath +
                            ", next " + item.filename());
            }
            if (current < 0) {
                current = static_cast<int>(index);
                states[index].started = true;
                states[index].output = std::make_unique<std::ofstream>(states[index].temporary,
                                                                       std::ios::binary | std::ios::trunc);
                if (!*states[index].output) {
                    return fail("cannot open download target: " + pathToUtf8(states[index].temporary));
                }
            }
            if (type == DAWNProto::DownloadFileAck_DownloadFileAckType_Data && !item.has_filedata()) {
                return fail("multi-file download item is missing fileData: " + item.filename());
            }
            if (item.has_filedata() && !item.filedata().empty()) {
                std::ofstream& output = *states[index].output;
                output.write(item.filedata().data(), static_cast<std::streamsize>(item.filedata().size()));
                if (!output) {
                    return fail("cannot write download target: " + pathToUtf8(states[index].temporary));
                }
            }
            if (type != DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone) {
                continue;
            }

            states[index].output->close();
            states[index].output.reset();
            if (!item.has_filehash() || item.filehash().empty()) {
                return fail("multi-file download ack is missing fileHash for " + item.filename());
            }
            std::string actualHash;
            std::string hashError;
            if (!fileSha1Upper(states[index].temporary, &actualHash, &hashError)) {
                return fail(hashError);
            }
            if (!equalsIgnoreCase(actualHash, item.filehash())) {
                return fail("multi-file download fileHash mismatch for " + item.filename() + ": expected " +
                            item.filehash() + ", got " + actualHash);
            }
            std::error_code ec;
            std::filesystem::remove(files[index].localPath, ec);
            std::filesystem::rename(states[index].temporary, files[index].localPath, ec);
            if (ec) {
                return fail("cannot commit download target: " + ec.message());
            }
            states[index].done = true;
            current = -1;
        }
    }

    const grpc::Status status = reader->Finish();
    if (current >= 0) {
        const std::string pending = files[static_cast<std::size_t>(current)].remotePath;
        cleanup();
        if (error != nullptr) {
            *error = "multi-file download stream ended before " + pending + " completed";
        }
        return false;
    }
    if (!status.ok()) {
        cleanup();
        if (error != nullptr) {
            *error = status.error_message();
        }
        return false;
    }
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (!states[i].done) {
            cleanup();
            if (error != nullptr) {
                *error = "agent did not provide file " + files[i].remotePath;
            }
            return false;
        }
    }
    return true;
}

bool AgentClient::downloadFiles(const std::string& agentAddress, const std::string& jobId,
                                const std::vector<AgentDownloadFile>& files, std::chrono::milliseconds timeout,
                                std::string* error) {
    if (files.empty()) {
        return true;
    }

    std::string multiError;
    if (downloadMultiFiles(agentAddress, jobId, files, timeout, &multiError)) {
        return true;
    }

    for (const AgentDownloadFile& file : files) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(file.localPath, ec)) {
            continue;
        }
        std::string singleError;
        if (!downloadFile(agentAddress, jobId, file.remotePath, file.localPath, timeout, &singleError)) {
            if (error != nullptr) {
                *error = "multi-file download failed (" + multiError + "); fallback " + file.remotePath + ": " +
                         singleError;
            }
            return false;
        }
    }
    return true;
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

    std::string hash;
    if (!fileSha1Upper(localPath, &hash, error)) {
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
    setString(&header, "hash", hash);
    if (!writer->Write(header)) {
        if (error != nullptr) {
            *error = "upload stream rejected header";
        }
        writer->WritesDone();
        writer->Finish();
        return false;
    }

    DAWNProto::UploadFileAck ack;
    if (!writer->Read(&ack)) {
        const grpc::Status status = writer->Finish();
        if (error != nullptr) {
            *error = status.ok() ? "upload stream ended before cache ack" : status.error_message();
        }
        return false;
    }
    const int initialType = getEnum(ack, "type");
    if (initialType == 3) {
        if (error != nullptr) {
            *error = hasField(ack, "message") ? getString(ack, "message") : "upload failed";
        }
        writer->WritesDone();
        writer->Finish();
        return false;
    }
    if (initialType == 0 || initialType == 1) {
        writer->WritesDone();
        const grpc::Status status = writer->Finish();
        if (!status.ok()) {
            if (error != nullptr) {
                *error = status.error_message();
            }
            return false;
        }
        return true;
    }
    if (initialType != 2) {
        if (error != nullptr) {
            *error = "unexpected upload ack type " + std::to_string(initialType);
        }
        writer->WritesDone();
        writer->Finish();
        return false;
    }

    std::vector<char> buffer(3u * 1024u * 1024u);
    bool ok = true;
    while (input) {
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
            if (error != nullptr) {
                *error = "upload stream rejected file data";
            }
            break;
        }
    }
    writer->WritesDone();

    bool done = false;
    while (writer->Read(&ack)) {
        const int type = getEnum(ack, "type");
        if (type == 0 || type == 1) {
            done = true;
        } else if (type == 3) {
            ok = false;
            if (error != nullptr) {
                *error = hasField(ack, "message") ? getString(ack, "message") : "upload failed";
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
    if (ok && !done) {
        if (error != nullptr) {
            *error = "upload stream ended without Done ack";
        }
        return false;
    }
    return ok;
}

} // namespace hbg::dawn
