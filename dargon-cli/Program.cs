﻿using Castle.DynamicProxy;
using Dargon.PortableObjects;
using Dargon.Services.Client;
using ItzWarty.Collections;
using ItzWarty.IO;
using ItzWarty.Networking;
using ItzWarty.Threading;
using System;
using System.Linq;
using System.Threading;
using Dargon.CLI.Interface;

namespace Dargon.CLI {
   public static class Program {
      public static int Main() {
         ICollectionFactory collectionFactory = new CollectionFactory();
         ProxyGenerator proxyGenerator = new ProxyGenerator();
         IServiceProxyFactory serviceProxyFactory = new ServiceProxyFactory(proxyGenerator);
         IServiceContextFactory serviceContextFactory = new ServiceContextFactory(collectionFactory);
         IThreadingFactory threadingFactory = new ThreadingFactory();
         ISynchronizationFactory synchronizationFactory = new SynchronizationFactory();
         IThreadingProxy threadingProxy = new ThreadingProxy(threadingFactory, synchronizationFactory);
         IDnsProxy dnsProxy = new DnsProxy();
         ITcpEndPointFactory tcpEndPointFactory = new TcpEndPointFactory(dnsProxy);
         IStreamFactory streamFactory = new StreamFactory();
         INetworkingInternalFactory networkingInternalFactory = new NetworkingInternalFactory(threadingProxy, streamFactory);
         ISocketFactory networkingProxy = new SocketFactory(tcpEndPointFactory, networkingInternalFactory);
         IInvocationStateFactory invocationStateFactory = new InvocationStateFactory(threadingProxy);
         IPofContext pofContext = new ClientPofContext();
         IPofSerializer pofSerializer = new PofSerializer(pofContext);
         IConnectorFactory connectorFactory = new ConnectorFactory(collectionFactory, threadingProxy, networkingProxy, invocationStateFactory, pofSerializer);

         var serviceConfiguration = new ClientServiceConfiguration();
         var serviceClientFactory = new ServiceClientFactory(collectionFactory, serviceProxyFactory, serviceContextFactory, connectorFactory);
         var localEndPoint = tcpEndPointFactory.CreateLoopbackEndPoint(serviceConfiguration.Port);
         var reconnectAttempts = 10;
         var reconnectDelay = 1000;
         var serviceClient = TryConnectToEndpoint(reconnectAttempts, reconnectDelay, serviceClientFactory, localEndPoint, serviceConfiguration);
         if (serviceClient == null) {
            Console.Error.WriteLine("Failed to connect to endpoint.");
            return 1;
         } else {
            var dispatcher = new DispatcherCommand("registered commands");
            dispatcher.RegisterCommand(new ShutdownCommand(serviceClient));
            dispatcher.RegisterCommand(new ModCommand(serviceClient));
            dispatcher.RegisterCommand(new ExitCommand());
            dispatcher.RegisterCommand(new ServiceCommand(serviceClient));

            var repl = new DargonREPL(dispatcher);
            repl.Run();
            return 0;
         }
      }

      private static IServiceClient TryConnectToEndpoint(int reconnectAttempts, int reconnectDelay, ServiceClientFactory serviceClientFactory, ITcpEndPoint endpoint, ClientServiceConfiguration serviceConfiguration) {
         IServiceClient serviceClient = null;
         for (var i = 0; i < reconnectAttempts && serviceClient == null; i++) {
            try {
               serviceClient = serviceClientFactory.Create(endpoint);
            } catch (Exception e) {
               Console.WriteLine(e);
               if (i == 0) {
                  Console.Write("Connecting to local endpoint on port " + serviceConfiguration.Port + ".");
               } else if (i > 0) {
                  Console.Write(".");
               }
               Thread.Sleep(reconnectDelay);
            }
            if (serviceClient != null && i > 0) {
               Console.WriteLine();
            }
         }
         return serviceClient;
      }
   }
}
