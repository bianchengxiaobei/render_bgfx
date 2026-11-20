using System.Collections.Concurrent;
using DAWNProto;
using Grpc.Core;

namespace DawnCoordinator;

internal static class Program
{
    private static int Main(string[] args)
    {
        var port = 5010;
        for (var i = 0; i < args.Length; ++i)
        {
            if (args[i] == "--port" && i + 1 < args.Length && int.TryParse(args[i + 1], out var parsed))
            {
                port = parsed;
            }
        }

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.SetMinimumLevel(LogLevel.Warning);
        builder.WebHost.ConfigureKestrel(options =>
        {
            options.ListenAnyIP(port, listen => listen.Protocols = Microsoft.AspNetCore.Server.Kestrel.Core.HttpProtocols.Http2);
        });
        builder.Services.AddGrpc();
        builder.Services.AddSingleton<MockCoordinator>();

        var app = builder.Build();
        app.MapGrpcService<MockCoordinator>();
        app.MapGet("/", () => "Dawn mock coordinator");
        MockCoordinator.Log($"listening on http://0.0.0.0:{port} (h2c)");
        app.Run();
        return 0;
    }
}

public sealed class MockCoordinator : CoordinatorService.CoordinatorServiceBase
{
    private static readonly object LogLock = new();
    private readonly ConcurrentDictionary<string, AgentInfo> _agents = new();
    private string _missionId = "";

    internal static void Log(string message)
    {
        lock (LogLock)
        {
            Console.WriteLine($"[coordinator] {DateTime.Now:HH:mm:ss.fff} {message}");
            Console.Out.Flush();
        }
    }

    private static GeneralAck Ok(string message = "") => new() { Code = AckCode.Ok, Message = message };

    private static string Brief(AgentInfo agent) =>
        agent == null ? "<none>" : $"{agent.Name}/{agent.ID}@{agent.Address} local={agent.IsLocalAgent}";

    public override Task<GeneralAck> RegisterAgent(RegisterAgentReq request, ServerCallContext context)
    {
        var agent = request.AgentInfo;
        if (agent == null || string.IsNullOrEmpty(agent.ID))
        {
            Log("RegisterAgent rejected: empty AgentInfo/ID");
            return Task.FromResult(new GeneralAck { Code = AckCode.Fail, Message = "empty AgentInfo/ID" });
        }

        _agents[agent.ID] = agent;
        Log($"RegisterAgent {Brief(agent)} category={agent.CategoryID} machine={agent.MachineID}");
        return Task.FromResult(Ok());
    }

    public override Task<GeneralAck> AgentPing(AgentPingReq request, ServerCallContext context)
    {
        var agent = request.AgentInfo;
        Log($"AgentPing {Brief(agent)} state={request.CurrentState} job={request.CurrentJobID}");
        return Task.FromResult(Ok());
    }

    public override Task<CheckJobAck> CheckJob(CheckJobReq request, ServerCallContext context)
    {
        Log($"CheckJob agent={request.AgentID} job={request.JobID} mission={request.MissionID}");
        return Task.FromResult(new CheckJobAck { Code = AckCode.Ok, Message = "" });
    }

    public override Task<GeneralAck> AgentLeaveJob(AgentLeaveJobReq request, ServerCallContext context)
    {
        Log($"AgentLeaveJob agent={request.AgentID} job={request.JobID}");
        return Task.FromResult(Ok());
    }

    public override Task<GeneralAck> AgentOffline(AgentOfflineReq request, ServerCallContext context)
    {
        Log($"AgentOffline agent={request.AgentID}");
        return Task.FromResult(Ok());
    }

    public override Task<GeneralAck> BeginMission(BeginMissionReq request, ServerCallContext context)
    {
        Log($"BeginMission(v1) mission={request.MissionID} category={request.CategoryID}");
        return Task.FromResult(Ok());
    }

    public override Task<BeginMissionV2Ack> BeginMissionV2(BeginMissionV2Req request, ServerCallContext context)
    {
        var mission = request.BeginMissionReq;
        _missionId = mission?.MissionID ?? "";
        Log($"BeginMissionV2 mission={_missionId} category={mission?.CategoryID} scheme={mission?.CalculationSchemeInfo?.Scheme}");
        return Task.FromResult(new BeginMissionV2Ack
        {
            GeneralAck = Ok(),
            TotalJobFileSizeLimitMB = 0,
            RedirectCoordinatorAddress = ""
        });
    }

    public override Task<GeneralAck> EndMission(EndMissionReq request, ServerCallContext context)
    {
        Log($"EndMission mission={request.MissionID} report={request.FinalReportMsg}");
        _missionId = "";
        return Task.FromResult(Ok());
    }

    public override Task<BeginJobAck> BeginJob(BeginJobReq request, ServerCallContext context)
    {
        var ack = new BeginJobAck { GeneralAck = Ok() };
        var local = _agents.Values.FirstOrDefault(agent => agent.IsLocalAgent);
        if (local != null)
        {
            ack.AllocatedAgentList.Add(new AllocatedAgent
            {
                ID = local.ID,
                Name = local.Name,
                Address = local.Address,
                IsLocalAgent = true
            });
        }
        Log($"BeginJob mission={request.MissionID} job={request.JobBasicDesc?.JobID} desired={request.DesiredNumAgents} allocated={ack.AllocatedAgentList.Count}");
        return Task.FromResult(ack);
    }

    public override Task<GeneralAck> EndJob(EndJobReq request, ServerCallContext context)
    {
        Log($"EndJob mission={request.MissionID} job={request.JobID} success={request.JobSuccess}");
        return Task.FromResult(Ok());
    }

    public override Task<JobPingAck> JobPing(JobPingReq request, ServerCallContext context)
    {
        Log($"JobPing mission={request.MissionID} job={request.JobID}");
        return Task.FromResult(new JobPingAck());
    }

    public override Task<QueryJobAck> QueryJob(QueryJobReq request, ServerCallContext context)
    {
        Log($"QueryJob mission={request.MissionID} job={request.JobID}");
        return Task.FromResult(new QueryJobAck { AllowAppend = false });
    }

    public override Task<GeneralAck> ChangeJob(ChangeJobReq request, ServerCallContext context)
    {
        Log($"ChangeJob mission={request.MissionID} job={request.JobID}");
        return Task.FromResult(Ok());
    }

    public override async Task RecordLog(IAsyncStreamReader<ClientLogReq> requestStream,
                                         IServerStreamWriter<GeneralAck> responseStream,
                                         ServerCallContext context)
    {
        while (await requestStream.MoveNext(context.CancellationToken))
        {
            var request = requestStream.Current;
            Log($"RecordLog mission={request.MissionID} user={request.UserInfo} {request.MessageLine}");
            await responseStream.WriteAsync(Ok());
        }
    }

    public override Task<AdminCmdAck> AdminCmd(AdminCmdReq request, ServerCallContext context)
    {
        Log($"AdminCmd cmd={request.Cmd}");
        return Task.FromResult(new AdminCmdAck { GeneralAck = Ok(), ResultString = "" });
    }

    public override Task<GeneralAck> MissionPing(MissionPingReq request, ServerCallContext context)
    {
        Log($"MissionPing mission={request.MissionID}");
        return Task.FromResult(Ok());
    }

    public override Task<QueryMissionAck> QueryMission(QueryMissionReq request, ServerCallContext context)
    {
        Log($"QueryMission mission={request.MissionID}");
        return Task.FromResult(new QueryMissionAck());
    }
}
