﻿using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Linq;
using Dargon.Client.Views;
using Dargon.Nest.Egg;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media.Animation;
using Castle.DynamicProxy;
using Dargon.Client.Controllers;
using Dargon.Client.ViewModels;
using Dargon.FileSystems;
using Dargon.IO;
using Dargon.IO.Drive;
using Dargon.RADS;
using ItzWarty;
using ItzWarty.IO;
using Dargon.IO.Resolution;
using Dargon.LeagueOfLegends.Modifications;
using Dargon.PortableObjects;
using Dargon.PortableObjects.Streams;
using Dargon.Services;
using ItzWarty.Collections;
using ItzWarty.Networking;
using ItzWarty.Threading;

namespace Dargon.Client {
   public class DargonClientEgg : INestApplicationEgg {
      private readonly IFileSystemProxy fileSystemProxy;
      private readonly DriveNodeFactory driveNodeFactory;
      private readonly RiotSolutionLoader riotSolutionLoader;
      private readonly LeagueModificationRepositoryService leagueModificationRepositoryService;
      private readonly List<object> keepalive = new List<object>(); 

      public DargonClientEgg() {
         IStreamFactory streamFactory = new StreamFactory();
         ICollectionFactory collectionFactory = new CollectionFactory();
         IThreadingProxy threadingProxy = new ThreadingProxy(new ThreadingFactory(), new SynchronizationFactory());
         IDnsProxy dnsProxy = new DnsProxy();
         INetworkingProxy networkingProxy = new NetworkingProxy(new SocketFactory(new TcpEndPointFactory(dnsProxy), new NetworkingInternalFactory(threadingProxy, streamFactory)), new TcpEndPointFactory(dnsProxy));
         fileSystemProxy = new FileSystemProxy(streamFactory);
         driveNodeFactory = new DriveNodeFactory(streamFactory);
         riotSolutionLoader = new RiotSolutionLoader();

         IPofContext pofContext = new ClientPofContext();
         IPofSerializer pofSerializer = new PofSerializer(pofContext);
         PofStreamsFactory pofStreamsFactory = new PofStreamsFactoryImpl(threadingProxy, streamFactory, pofSerializer);

         IClusteringConfiguration clusteringConfiguration = new ClientClusteringConfiguration();
         IServiceClientFactory serviceClientFactory = new ServiceClientFactory(new ProxyGenerator(), streamFactory, collectionFactory, threadingProxy, networkingProxy, pofSerializer, pofStreamsFactory);
         IServiceClient localServiceClient = serviceClientFactory.CreateOrJoin(clusteringConfiguration);
         keepalive.Add(localServiceClient);

         leagueModificationRepositoryService = localServiceClient.GetService<LeagueModificationRepositoryService>();
      }

      public NestResult Start(IEggParameters parameters) {
         var userInterfaceThread = new Thread(UserInterfaceThreadStart);
         userInterfaceThread.SetApartmentState(ApartmentState.STA);
         userInterfaceThread.Start();
         return NestResult.Success;
      }

      private void UserInterfaceThreadStart() {
         var application = Application.Current ?? new Application();
         var dispatcher = application.Dispatcher;
         var window = new MainWindow();
         ObservableCollection<ModificationViewModel> modifications = new ObservableCollection<ModificationViewModel>();
         modifications.Add(new ModificationViewModel {
            Name = "Something Ezreal",
            Author = "Herp",
            Status = ModificationStatus.Disabled,
            Type = ModificationType.Champion
         });
         modifications.Add(new ModificationViewModel {
            Name = "SR But Better",
            Author = "Lerp",
            Status = ModificationStatus.Enabled,
            Type = ModificationType.Map
         });
         modifications.Add(new ModificationViewModel {
            Name = "Warty the Ward",
            Author = "ijofgsdojmn",
            Status = ModificationStatus.Enabled,
            Type = ModificationType.Ward
         });
         modifications.Add(new ModificationViewModel {
            Name = "ItBlinksAlot UI",
            Author = "23erp",
            Status = ModificationStatus.UpdateAvailable,
            Type = ModificationType.UI
         });
         modifications.Add(new ModificationViewModel {
            Name = "Something Else",
            Author = "Perp",
            Status = ModificationStatus.Broken,
            Type = ModificationType.Other
         });
         var modificationImportViewModelFactory = new ModificationImportViewModelFactory(fileSystemProxy, driveNodeFactory);
         var rootViewModelCommandFactory = new ModificationImportController(leagueModificationRepositoryService, riotSolutionLoader, modificationImportViewModelFactory);
         var rootViewModel = new RootViewModel(rootViewModelCommandFactory, window, modifications);
         window.DataContext = rootViewModel;
         application.Run(window);
      }

      public NestResult Shutdown() {
         return NestResult.Success;
      }
   }
}