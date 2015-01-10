﻿using Castle.DynamicProxy;
using Dargon.FinalFantasyXIII;
using Dargon.Game;
using Dargon.InjectedModule;
using Dargon.LeagueOfLegends;
using Dargon.Management.Server;
using Dargon.ModificationRepositories;
using Dargon.Modifications;
using Dargon.PortableObjects;
using Dargon.Processes.Injection;
using Dargon.Processes.Watching;
using Dargon.Services;
using Dargon.Services.Server;
using Dargon.Services.Server.Phases;
using Dargon.Services.Server.Sessions;
using Dargon.Transport;
using Dargon.Tray;
using ItzWarty;
using ItzWarty.Collections;
using ItzWarty.IO;
using ItzWarty.Networking;
using ItzWarty.Processes;
using ItzWarty.Threading;
using NLog;
using NLog.Config;
using NLog.Targets;
using NLog.Targets.Wrappers;

namespace Dargon.Daemon
{
   public static class Program {
      private static readonly Logger logger = LogManager.GetCurrentClassLogger();
      private const int kDaemonManagementPort = 21000;

      public static void Main(string[] args) { 
         InitializeLogging();

#if DEBUG
         logger.Error("COMPILED IN DEBUG MODE");
#endif

         // construct libwarty dependencies
         ICollectionFactory collectionFactory = new CollectionFactory();
         
         // construct libwarty-proxies dependencies
         IStreamFactory streamFactory = new StreamFactory();
         IFileSystemProxy fileSystemProxy = new FileSystemProxy(streamFactory);
         IThreadingFactory threadingFactory = new ThreadingFactory();
         ISynchronizationFactory synchronizationFactory = new SynchronizationFactory();
         IThreadingProxy threadingProxy = new ThreadingProxy(threadingFactory, synchronizationFactory);
         IDnsProxy dnsProxy = new DnsProxy();
         ITcpEndPointFactory tcpEndPointFactory = new TcpEndPointFactory(dnsProxy);
         INetworkingInternalFactory networkingInternalFactory = new NetworkingInternalFactory(threadingProxy, streamFactory);
         ISocketFactory socketFactory = new SocketFactory(tcpEndPointFactory, networkingInternalFactory);
         INetworkingProxy networkingProxy = new NetworkingProxy(socketFactory, tcpEndPointFactory);
         IProcessProxy processProxy = new ProcessProxy();

         // construct Castle.Core dependencies
         ProxyGenerator proxyGenerator = new ProxyGenerator();

         // construct dargon common Portable Object Format dependencies
         IPofContext pofContext = new ClientPofContext();
         IPofSerializer pofSerializer = new PofSerializer(pofContext);

         // construct libdargon.management dependencies
         ITcpEndPoint managementServerEndpoint = networkingProxy.CreateAnyEndPoint(kDaemonManagementPort);
         IMessageFactory managementServerMessageFactory = new MessageFactory();
         IManagementSessionFactory managementSessionFactory = new ManagementSessionFactory(collectionFactory, threadingProxy, pofSerializer, managementServerMessageFactory);
         IManagementContextFactory managementContextFactory = new ManagementContextFactory(pofContext);
         ILocalManagementServerContext localManagementServerContext = new LocalManagementServerContext(collectionFactory, managementSessionFactory);
         ILocalManagementRegistry localManagementServerRegistry = new LocalManagementRegistry(pofSerializer, managementContextFactory, localManagementServerContext);
         IManagementServerConfiguration localManagementServerConfiguration = new ManagementServerConfiguration(managementServerEndpoint);
         var localManagementServer = new LocalManagementServer(threadingProxy, networkingProxy, managementSessionFactory, localManagementServerContext, localManagementServerConfiguration);
         localManagementServer.Initialize();

         // construct root Dargon dependencies.
         var configuration = new ClientConfiguration();

         // construct system-state dependencies
         var systemState = new ClientSystemStateImpl(fileSystemProxy, configuration.ConfigurationDirectoryPath);
         localManagementServerRegistry.RegisterInstance(new ClientSystemStateMob(systemState));
         
         // construct libdsp dependencies
         IHostSessionFactory hostSessionFactory = new HostSessionFactory(collectionFactory, pofSerializer);
         IPhaseFactory phaseFactory = new PhaseFactory(collectionFactory, threadingProxy, networkingProxy, hostSessionFactory, pofSerializer);
         IConnectorFactory connectorFactory = new ConnectorFactory(collectionFactory, threadingProxy, networkingProxy, phaseFactory);
         IServiceContextFactory serviceContextFactory = new ServiceContextFactory(collectionFactory);
         IServiceNodeFactory serviceNodeFactory = new ServiceNodeFactory(connectorFactory, serviceContextFactory, collectionFactory);

         // construct libdsp local service node
         IServiceConfiguration serviceConfiguration = new ClientServiceConfiguration();
         IServiceNode localServiceNode = serviceNodeFactory.CreateOrJoin(serviceConfiguration);

         // construct Dargon Daemon dependencies
         var core = new DaemonServiceImpl(configuration);
         DaemonService daemonService = core;
         localServiceNode.RegisterService(daemonService, typeof(DaemonService));
         localManagementServerRegistry.RegisterInstance(new DaemonServiceMob(core));

         // construct miscellanious common Dargon dependencies
         TemporaryFileService temporaryFileService = new TemporaryFileServiceImpl(configuration);
         IDtpNodeFactory dtpNodeFactory = new DefaultDtpNodeFactory();

         // construct modification and repository dependencies
         IModificationMetadataSerializer modificationMetadataSerializer = new ModificationMetadataSerializer(fileSystemProxy);
         IModificationMetadataFactory modificationMetadataFactory = new ModificationMetadataFactory();
         IBuildConfigurationLoader buildConfigurationLoader = new BuildConfigurationLoader();
         IModificationLoader modificationLoader = new ModificationLoader(modificationMetadataSerializer, buildConfigurationLoader);
         ModificationRepositoryService modificationRepositoryService = new ModificationRepositoryServiceImpl(configuration, fileSystemProxy, modificationLoader, modificationMetadataSerializer, modificationMetadataFactory).With(s => s.Initialize());
         localServiceNode.RegisterService(modificationRepositoryService, typeof(ModificationRepositoryService));

         // construct process watching/injection dependencies
         IProcessInjector processInjector = new ProcessInjector();
         IProcessDiscoveryMethodFactory processDiscoveryMethodFactory = new ProcessDiscoveryMethodFactory();
         IProcessDiscoveryMethod processDiscoveryMethod = processDiscoveryMethodFactory.CreateOptimalProcessDiscoveryMethod();
         IProcessWatcher processWatcher = new ProcessWatcher(processProxy, processDiscoveryMethod);
         TrayService trayService = new TrayServiceImpl(daemonService);
         IProcessInjectionConfiguration processInjectionConfiguration = new ProcessInjectionConfiguration(100, 200);
         ProcessInjectionService processInjectionService = new ProcessInjectionServiceImpl(processInjector, processInjectionConfiguration);
         ProcessWatcherService processWatcherService = new ProcessWatcherServiceImpl(processProxy, processWatcher).With(s => s.Initialize());
         ISessionFactory sessionFactory = new SessionFactory(dtpNodeFactory);
         IInjectedModuleServiceConfiguration injectedModuleServiceConfiguration = new InjectedModuleServiceConfiguration();
         InjectedModuleService injectedModuleService = new InjectedModuleServiceImpl(processInjectionService, sessionFactory, injectedModuleServiceConfiguration).With(x => x.Initialize());
         localServiceNode.RegisterService(injectedModuleService, typeof(InjectedModuleService));
         localServiceNode.RegisterService(processInjectionService, typeof(ProcessInjectionService));

         // construct additional Dargon dependencies
         IGameHandler leagueGameServiceImpl = new LeagueGameServiceImpl(threadingProxy, fileSystemProxy, localManagementServerRegistry, daemonService, temporaryFileService, processProxy, injectedModuleService, processWatcherService, modificationRepositoryService);
         IGameHandler ffxiiiGameServiceImpl = new FFXIIIGameServiceImpl(daemonService, processProxy, injectedModuleService, processWatcherService);
         core.Run();
      }

      private static void InitializeLogging()
      {
         var config = new LoggingConfiguration();
         Target debuggerTarget = new DebuggerTarget();
         Target consoleTarget = new ColoredConsoleTarget();

#if !DEBUG
         debuggerTarget = new AsyncTargetWrapper(debuggerTarget);
         consoleTarget = new AsyncTargetWrapper(consoleTarget);
#endif

         config.AddTarget("debugger", debuggerTarget);
         config.AddTarget("console", consoleTarget);

         var debuggerRule = new LoggingRule("*", LogLevel.Trace, debuggerTarget);
         config.LoggingRules.Add(debuggerRule);

         var consoleRule = new LoggingRule("*", LogLevel.Trace, consoleTarget);
         config.LoggingRules.Add(consoleRule);

         LogManager.Configuration = config;
      }
   }
}
