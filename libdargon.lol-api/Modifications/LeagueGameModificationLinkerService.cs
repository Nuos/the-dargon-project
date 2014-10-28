﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Dargon.Modifications;

namespace Dargon.LeagueOfLegends.Modifications
{
   public interface LeagueGameModificationLinkerService
   {
      void LinkModificationObjects();
   }

   public interface LeagueGameModificationLinkerResult
   {
      IReadOnlyDictionary<uint, Tuple<string, string>> ArchiveAndDataOverridesById { get; }
      string ReleaseManifestOverridePath { get; }
   }
}