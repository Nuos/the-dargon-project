﻿using System;
using System.Collections.Generic;
using System.Diagnostics;
using ItzWarty;
using ItzWarty.Services;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using NMockito;

namespace Dargon.Processes.Watching
{
   [TestClass]
   public class ProcessWatcherServiceImplTest : NMockitoInstance
   {
      private ProcessWatcherServiceImpl testObj;

      [Mock] private readonly IServiceLocator serviceLocator = null;
      [Mock] private readonly IProcessProxy processProxy = null;
      [Mock] private readonly IProcessWatcher processWatcher = null;

      private const string NOTEPAD = "notepad.exe";
      private const string WORD = "msword.exe";
      private const string CHROME = "chrome.exe";
      private const string MAPLESTORY = "maplestory.exe";
      private const string STEAM = "steam.exe";

      private const int PARENT_PID = 19047;
      private const int NOTEPAD_PID = 1;
      private const int CHROME_PID = 2;
      private const int MAPLESTORY_PID = 3;
      private const int STEAM_PID = 4;

      private readonly CreatedProcessDescriptor notepadCpd = new CreatedProcessDescriptor(NOTEPAD, NOTEPAD_PID, PARENT_PID);
      private readonly CreatedProcessDescriptor chromeCpd = new CreatedProcessDescriptor(CHROME, CHROME_PID, PARENT_PID);
      private readonly CreatedProcessDescriptor maplestoryCpd = new CreatedProcessDescriptor(MAPLESTORY, MAPLESTORY_PID, PARENT_PID);
      private readonly CreatedProcessDescriptor steamCpd = new CreatedProcessDescriptor(STEAM, STEAM_PID, PARENT_PID);

      [Mock] private IConsumer a, b, c;

      [TestInitialize]
      public void Setup()
      {
         InitializeMocks();

         testObj = new ProcessWatcherServiceImpl(serviceLocator, processProxy, processWatcher);

         testObj.Subscribe(a.Consume, new[] { CHROME, MAPLESTORY }, false);
         testObj.Subscribe(b.Consume, new[] { CHROME, NOTEPAD, WORD }, false);
         testObj.Subscribe(c.Consume, new[] { NOTEPAD, CHROME, WORD }, false);
         ClearInteractions(testObj);
      }

      [TestMethod]
      public void InitializeHappyPathTest()
      {
         testObj.Initialize();

         Verify(serviceLocator).RegisterService(typeof(ProcessWatcherService), testObj);
         Verify(processWatcher).Start();
         ClearInteractions(processWatcher, 1); // TODO: Subscription to NewProcessFound
         VerifyNoMoreInteractions();
      }

      [TestMethod]
      public void HandleProcessWatcherNewProcessFoundOneMatchingConsumerTest()
      {
         testObj.HandleProcessWatcherNewProcessFound(processWatcher, new ProcessFoundEventArgs(MAPLESTORY, MAPLESTORY_PID, PARENT_PID));
         Verify(a).Consume(maplestoryCpd);
         VerifyNoMoreInteractions(b);
         VerifyNoMoreInteractions(c);
      }

      [TestMethod]
      public void HandleProcessWatcherNewProcessFoundNoMatchingConsumersTest()
      {
         testObj.HandleProcessWatcherNewProcessFound(processWatcher, new ProcessFoundEventArgs(STEAM, STEAM_PID, PARENT_PID));
         VerifyNoMoreInteractions(a);
         VerifyNoMoreInteractions(b);
         VerifyNoMoreInteractions(c);
      }

      [TestMethod]
      public void HandleProcessWatcherNewProcessFoundOneMatchingConsumerTestAllMatchingConsumersTest()
      {
         testObj.HandleProcessWatcherNewProcessFound(processWatcher, new ProcessFoundEventArgs(CHROME, CHROME_PID, PARENT_PID));
         Verify(a).Consume(chromeCpd);
         Verify(b).Consume(chromeCpd);
         Verify(c).Consume(chromeCpd);
      }

      [TestMethod]
      public void HandleProcessWatcherNewProcessFoundOneMatchingConsumerTwoMatchingConsumersTest()
      {
         testObj.HandleProcessWatcherNewProcessFound(processWatcher, new ProcessFoundEventArgs(NOTEPAD, NOTEPAD_PID, PARENT_PID));
         VerifyNoMoreInteractions(a);
         Verify(b).Consume(notepadCpd);
         Verify(c).Consume(notepadCpd);
      }

      [TestMethod]
      public void SubscribeRetroactiveDiscoveryTest()
      {
         const int EXPLORER_PID = 1235;

         var notepadProcess = CreateMock<IProcess>();
         When(notepadProcess.Id).ThenReturn(NOTEPAD_PID);
         When(notepadProcess.ProcessName).ThenReturn(NOTEPAD);
         var explorerProcess = CreateMock<IProcess>();
         When(explorerProcess.Id).ThenReturn(EXPLORER_PID);
         When(processProxy.GetParentProcess(notepadProcess)).ThenReturn(explorerProcess);

         var matches = new List<IProcess> { notepadProcess };
         When(processWatcher.FindProcess(Any<Func<IProcess, bool>>())).ThenReturn(matches);
         testObj.Subscribe(a.Consume, new[] { NOTEPAD }, true);
         Verify(a).Consume(new CreatedProcessDescriptor(NOTEPAD, NOTEPAD_PID, EXPLORER_PID));
      }

      public interface IConsumer
      {
         void Consume(CreatedProcessDescriptor descriptor);
      }
   }
}