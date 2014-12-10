#include "stdafx.h"
#include <iostream>
#include "util.hpp"
#include "IO/DSP/DSPExNodeSession.hpp"
#include "Init/bootstrap_context.hpp"
#include "../Subsystem.hpp"
#include "../Subsystem.Detours.hpp"
#include "FileSubsystem.hpp"
#include "FileSubsystemTypedefs.hpp"
#include "FileOverrideTaskHandler.hpp"
#include "FileSwapTaskHandler.hpp"
#include "../IO/DIM/CommandManager.hpp"

using namespace dargon::Subsystems;

const bool kDebugEnabled = true;

FileSubsystem::FileSubsystem(std::shared_ptr<dargon::IO::DIM::CommandManager> command_manager)
   : command_manager(command_manager), file_swap_task_handler(std::make_shared<FileSwapTaskHandler>(this)) { }

bool FileSubsystem::Initialize()
{
   std::cout << "At FileSubsystem Init with m_initialized" << m_initialized << std::endl;
   if(m_initialized) return true;
   else
   {
      Subsystem::Initialize();

      // Register DIM Task Handlers
      command_manager->RegisterTaskHandler(file_swap_task_handler.get());

      // Ensure we've been told to initialize
      if(std::find(s_bootstrap_context->argument_flags.begin(),
                   s_bootstrap_context->argument_flags.end(),
                   "--enable-filesystem-hooks") == s_bootstrap_context->argument_flags.end())
      {
         std::cout << "At FileSubsystem Init but --enable-filesystem-hooks not set" << std::endl;
         return false;
      }

      HMODULE hModuleKernel32 = WaitForModuleHandle("Kernel32.dll");
      InstallCreateEventADetour(hModuleKernel32);
      InstallCreateEventWDetour(hModuleKernel32);
      InstallCreateFileADetour(hModuleKernel32);
      InstallCreateFileWDetour(hModuleKernel32);
      InstallReadFileDetour(hModuleKernel32);
      InstallWriteFileDetour(hModuleKernel32);
      InstallCloseHandleDetour(hModuleKernel32);
      InstallSetFilePointerDetour(hModuleKernel32);
      s_bootstrap_context->io_proxy->__Override(m_trampCreateEventA, m_trampCreateEventW, m_trampCreateFileA, m_trampCreateFileW, m_trampReadFile, m_trampWriteFile, m_trampCloseHandle, m_trampSetFilePointer);
      return true;
   }
}

bool FileSubsystem::Uninitialize()
{
   if(!m_initialized) return true;
   else
   {
      Subsystem::Uninitialize();

      // Unregister Task Handlers
      command_manager->UnregisterTaskHandler(file_swap_task_handler.get());

      UninstallCreateEventADetour();
      UninstallCreateEventWDetour();
      UninstallCreateFileADetour();
      UninstallCreateFileWDetour();
      UninstallReadFileDetour();
      UninstallWriteFileDetour();
      UninstallCloseHandleDetour();
      UninstallSetFilePointerDetour();
      return true;
   }
}

void FileSubsystem::AddFileOverride(FileOverrideTargetDescriptor descriptor, FileOverride fileOverride)
{
   s_fileOverridesMap.insert(
      FileOverrideMap::value_type(
         descriptor,
         fileOverride
      )
   );
}

// - static ---------------------------------------------------------------------------------------
FileOverrideMap FileSubsystem::s_fileOverridesMap;
AdvancedOverrideMap FileSubsystem::s_advancedOverridesMap;
dargon::concurrent_set<HANDLE> FileSubsystem::mitmHandles;

DIM_IMPL_STATIC_DETOUR(FileSubsystem, CreateEventA, FunctionCreateEventA, "CreateEventA", MyCreateEventA);
DIM_IMPL_STATIC_DETOUR(FileSubsystem, CreateEventW, FunctionCreateEventW, "CreateEventW", MyCreateEventW);
DIM_IMPL_STATIC_DETOUR(FileSubsystem, CreateFileA, FunctionCreateFileA, "CreateFileA", MyCreateFileA);
DIM_IMPL_STATIC_DETOUR(FileSubsystem, CreateFileW, FunctionCreateFileW, "CreateFileW", MyCreateFileW);
DIM_IMPL_STATIC_DETOUR(FileSubsystem, ReadFile, FunctionReadFile, "ReadFile", MyReadFile);
DIM_IMPL_STATIC_DETOUR(FileSubsystem, WriteFile, FunctionWriteFile, "WriteFile", MyWriteFile);
DIM_IMPL_STATIC_DETOUR(FileSubsystem, CloseHandle, FunctionCloseHandle, "CloseHandle", MyCloseHandle);
DIM_IMPL_STATIC_DETOUR(FileSubsystem, SetFilePointer, FunctionSetFilePointer, "SetFilePointer", MySetFilePointer);

HANDLE WINAPI FileSubsystem::MyCreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName) {
   return m_trampCreateEventA(lpEventAttributes, bManualReset, bInitialState, lpName);
}
HANDLE WINAPI FileSubsystem::MyCreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName) {
   return m_trampCreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName);
}

HANDLE WINAPI FileSubsystem::MyCreateFileA(LPCSTR lpFilePath, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
   if (kDebugEnabled) {
      s_logger->Log(
         LL_VERBOSE, 
         [&](std::ostream& os){ 
         os << "Detour MyCreateFileA:"
            << " lpFilePath: " << lpFilePath
            << " dwDesiredAccess: " << dwDesiredAccess
            << " dwShareMode: " << dwShareMode
            << " lpSecurityAttributes: " << lpSecurityAttributes
            << " dwCreationDisposition: " << dwCreationDisposition
            << " dwFlagsAndAttributes: " << dwFlagsAndAttributes
            << " hTemplateFile: " << hTemplateFile
            << std::endl;
      });
   }
   auto fileHandle = m_trampCreateFileA(lpFilePath, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
   mitmHandles.insert(fileHandle);

   BY_HANDLE_FILE_INFORMATION fileInfo;
   GetFileInformationByHandle(fileHandle, &fileInfo);
   
   FileOverrideTargetDescriptor descriptor;
   descriptor.targetFileIndexHigh = fileInfo.nFileIndexHigh;
   descriptor.targetFileIndexLow = fileInfo.nFileIndexLow;
   descriptor.targetVolumeSerialNumber = fileInfo.dwVolumeSerialNumber;

   auto override = s_fileOverridesMap.find(descriptor);
   if(override != s_fileOverridesMap.end())
   {
      if (override->second.isFullSwap)
      {
         m_realCloseHandle(fileHandle);
         mitmHandles.remove(fileHandle);
         fileHandle = m_trampCreateFileA(override->second.replacementPath.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
         mitmHandles.insert(fileHandle);
      }
      else // advanced override
      {
         auto tree = override->second.pOverrideTree;
         HANDLE fileHandleReplacement = m_trampCreateFileA(override->second.replacementPath.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

         // If the replacement doesn't exist, nothing special happens. Otherwise, 
         // register advanced override.
         if (fileHandleReplacement != INVALID_HANDLE_VALUE)
         {
            s_advancedOverridesMap.insert(
               AdvancedOverrideMap::value_type(
                  fileHandle,
                  FileOverrideInstanceDescription(fileHandleReplacement, tree)
               )
            );
         }
      }
   }
   return fileHandle;
}

HANDLE WINAPI FileSubsystem::MyCreateFileW(LPCWSTR lpFilePath, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
   if (kDebugEnabled) {
      s_logger->Log(
         LL_VERBOSE,
         [=](std::ostream& os){
         os << "CreateFileW:"
            << " lpFilePath: " << dargon::narrow(lpFilePath)
            << " dwDesiredAccess: " << dwDesiredAccess
            << " dwShareMode: " << dwShareMode
            << " lpSecurityAttributes: " << lpSecurityAttributes
            << " dwCreationDisposition: " << dwCreationDisposition
            << " dwFlagsAndAttributes: " << dwFlagsAndAttributes
            << " hTemplateFile: " << hTemplateFile
            << std::endl;
      });
   }
   auto fileHandle = m_trampCreateFileW(lpFilePath, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
   mitmHandles.insert(fileHandle);

   BY_HANDLE_FILE_INFORMATION fileInfo;
   GetFileInformationByHandle(fileHandle, &fileInfo);
   
   FileOverrideTargetDescriptor descriptor;
   descriptor.targetFileIndexHigh = fileInfo.nFileIndexHigh;
   descriptor.targetFileIndexLow = fileInfo.nFileIndexLow;
   descriptor.targetVolumeSerialNumber = fileInfo.dwVolumeSerialNumber;

   auto override = s_fileOverridesMap.find(descriptor);
   if(override != s_fileOverridesMap.end())
   {
      if (override->second.isFullSwap)
      {
         mitmHandles.remove(fileHandle);
         CloseHandle(fileHandle);
         fileHandle = m_trampCreateFileA(override->second.replacementPath.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
         mitmHandles.insert(fileHandle);
      }
      else // advanced override
      {
         auto tree = override->second.pOverrideTree;
         HANDLE fileHandleReplacement = m_trampCreateFileA(override->second.replacementPath.c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

         // If the replacement doesn't exist, nothing special happens. Otherwise, 
         // register advanced override.
         if (fileHandleReplacement != INVALID_HANDLE_VALUE)
         {
            s_advancedOverridesMap.insert(
               AdvancedOverrideMap::value_type(
                  fileHandle,
                  FileOverrideInstanceDescription(fileHandleReplacement, tree)
               )
            );
         }
      }
   }
   return fileHandle;
}

BOOL WINAPI FileSubsystem::MyReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
   auto mitm = mitmHandles.contains(hFile);
   if (mitm) {
      if (kDebugEnabled) {
         s_logger->Log(
            LL_VERBOSE,
            [=](std::ostream& os) {
            os << "ReadFile: hFile: " << hFile
               << " nNumberOfBytesToRead: " << nNumberOfBytesToRead
               << " lpNumberOfBytesRead: " << lpNumberOfBytesRead
               << " lpOverlapped: " << lpOverlapped << std::endl;
         });
      }
   }
   BOOL result = m_trampReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
   return result;
}

BOOL WINAPI FileSubsystem::MyWriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped) {
   return m_trampWriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

BOOL WINAPI FileSubsystem::MyCloseHandle(HANDLE hObject)
{
   auto mitm = mitmHandles.remove(hObject);
   if (mitm) {
      if (kDebugEnabled) {
         s_logger->Log(
            LL_VERBOSE,
            [=](std::ostream& os) {
            os << "CloseHandle: hObject: " << hObject << std::endl;
         });
      }
   }
   BOOL result = m_trampCloseHandle(hObject);
   return result;
}
DWORD WINAPI FileSubsystem::MySetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
   auto mitm = mitmHandles.contains(hFile);
   if (mitm) {
      if (kDebugEnabled) {
         s_logger->Log(
            LL_VERBOSE,
            [=](std::ostream& os) {
            os << "SetFilePointer: hFile: " << hFile
               << " lDistanceToMove: " << lDistanceToMove
               << " lpDistanceToMoveHigh: " << lpDistanceToMoveHigh
               << " dwMoveMethod: " << dwMoveMethod << std::endl;
         });
      }

   }
   DWORD result = m_trampSetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
   return result;
}