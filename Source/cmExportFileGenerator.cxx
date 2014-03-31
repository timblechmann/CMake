/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#include "cmExportFileGenerator.h"

#include "cmExportSet.h"
#include "cmGeneratedFileStream.h"
#include "cmGlobalGenerator.h"
#include "cmInstallExportGenerator.h"
#include "cmLocalGenerator.h"
#include "cmMakefile.h"
#include "cmSystemTools.h"
#include "cmTarget.h"
#include "cmTargetExport.h"
#include "cmVersion.h"
#include "cmComputeLinkInformation.h"

#include <cmsys/auto_ptr.hxx>
#include <cmsys/FStream.hxx>
#include <assert.h>

//----------------------------------------------------------------------------
cmExportFileGenerator::cmExportFileGenerator()
{
  this->AppendMode = false;
  this->ExportOld = false;
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::AddConfiguration(const std::string& config)
{
  this->Configurations.push_back(config);
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::SetExportFile(const char* mainFile)
{
  this->MainImportFile = mainFile;
  this->FileDir =
    cmSystemTools::GetFilenamePath(this->MainImportFile);
  this->FileBase =
    cmSystemTools::GetFilenameWithoutLastExtension(this->MainImportFile);
  this->FileExt =
    cmSystemTools::GetFilenameLastExtension(this->MainImportFile);
}

//----------------------------------------------------------------------------
const char* cmExportFileGenerator::GetMainExportFileName() const
{
  return this->MainImportFile.c_str();
}

//----------------------------------------------------------------------------
bool cmExportFileGenerator::GenerateImportFile()
{
  // Open the output file to generate it.
  cmsys::auto_ptr<cmsys::ofstream> foutPtr;
  if(this->AppendMode)
    {
    // Open for append.
    cmsys::auto_ptr<cmsys::ofstream>
      ap(new cmsys::ofstream(this->MainImportFile.c_str(), std::ios::app));
    foutPtr = ap;
    }
  else
    {
    // Generate atomically and with copy-if-different.
    cmsys::auto_ptr<cmGeneratedFileStream>
      ap(new cmGeneratedFileStream(this->MainImportFile.c_str(), true));
    ap->SetCopyIfDifferent(true);
    foutPtr = ap;
    }
  if(!foutPtr.get() || !*foutPtr)
    {
    std::string se = cmSystemTools::GetLastSystemError();
    cmOStringStream e;
    e << "cannot write to file \"" << this->MainImportFile
      << "\": " << se;
    cmSystemTools::Error(e.str().c_str());
    return false;
    }
  std::ostream& os = *foutPtr;

  // Protect that file against use with older CMake versions.
  os << "# Generated by CMake " << cmVersion::GetCMakeVersion() << "\n\n";
  os << "if(\"${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}\" LESS 2.5)\n"
     << "   message(FATAL_ERROR \"CMake >= 2.6.0 required\")\n"
     << "endif()\n";

  // Isolate the file policy level.
  // We use 2.6 here instead of the current version because newer
  // versions of CMake should be able to export files imported by 2.6
  // until the import format changes.
  os << "cmake_policy(PUSH)\n"
     << "cmake_policy(VERSION 2.6)\n";

  // Start with the import file header.
  this->GenerateImportHeaderCode(os);

  // Create all the imported targets.
  bool result = this->GenerateMainFile(os);

  // End with the import file footer.
  this->GenerateImportFooterCode(os);
  os << "cmake_policy(POP)\n";

  return result;
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::GenerateImportConfig(std::ostream& os,
                                    const std::string& config,
                                    std::vector<std::string> &missingTargets)
{
  // Construct the property configuration suffix.
  std::string suffix = "_";
  if(!config.empty())
    {
    suffix += cmSystemTools::UpperCase(config);
    }
  else
    {
    suffix += "NOCONFIG";
    }

  // Generate the per-config target information.
  this->GenerateImportTargetsConfig(os, config, suffix, missingTargets);
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::PopulateInterfaceProperty(
                                              const std::string& propName,
                                              cmTarget *target,
                                              ImportPropertyMap &properties)
{
  const char *input = target->GetProperty(propName);
  if (input)
    {
    properties[propName] = input;
    }
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::PopulateInterfaceProperty(
                      const std::string& propName,
                      const std::string& outputName,
                      cmTarget *target,
                      cmGeneratorExpression::PreprocessContext preprocessRule,
                      ImportPropertyMap &properties,
                      std::vector<std::string> &missingTargets)
{
  const char *input = target->GetProperty(propName);
  if (input)
    {
    if (!*input)
      {
      // Set to empty
      properties[outputName] = "";
      return;
      }

    std::string prepro = cmGeneratorExpression::Preprocess(input,
                                                           preprocessRule);
    if (!prepro.empty())
      {
      this->ResolveTargetsInGeneratorExpressions(prepro, target,
                                                 missingTargets);
      properties[outputName] = prepro;
      }
    }
}

void cmExportFileGenerator::GenerateRequiredCMakeVersion(std::ostream& os,
                                                    const char *versionString)
{
  os << "if(CMAKE_VERSION VERSION_LESS " << versionString << ")\n"
        "  message(FATAL_ERROR \"This file relies on consumers using "
        "CMake " << versionString << " or greater.\")\n"
        "endif()\n\n";
}

//----------------------------------------------------------------------------
bool cmExportFileGenerator::PopulateInterfaceLinkLibrariesProperty(
                      cmTarget *target,
                      cmGeneratorExpression::PreprocessContext preprocessRule,
                      ImportPropertyMap &properties,
                      std::vector<std::string> &missingTargets)
{
  if(!target->IsLinkable())
    {
    return false;
    }
  const char *input = target->GetProperty("INTERFACE_LINK_LIBRARIES");
  if (input)
    {
    std::string prepro = cmGeneratorExpression::Preprocess(input,
                                                           preprocessRule);
    if (!prepro.empty())
      {
      this->ResolveTargetsInGeneratorExpressions(prepro, target,
                                                 missingTargets,
                                                 ReplaceFreeTargets);
      properties["INTERFACE_LINK_LIBRARIES"] = prepro;
      return true;
      }
    }
  return false;
}

//----------------------------------------------------------------------------
static bool isSubDirectory(const char* a, const char* b)
{
  return (cmSystemTools::ComparePath(a, b) ||
          cmSystemTools::IsSubDirectory(a, b));
}

//----------------------------------------------------------------------------
static bool checkInterfaceDirs(const std::string &prepro,
                      cmTarget *target)
{
  const char* installDir =
            target->GetMakefile()->GetSafeDefinition("CMAKE_INSTALL_PREFIX");
  const char* topSourceDir = target->GetMakefile()->GetHomeDirectory();
  const char* topBinaryDir = target->GetMakefile()->GetHomeOutputDirectory();

  std::vector<std::string> parts;
  cmGeneratorExpression::Split(prepro, parts);

  const bool inSourceBuild = strcmp(topSourceDir, topBinaryDir) == 0;

  bool hadFatalError = false;

  for(std::vector<std::string>::iterator li = parts.begin();
      li != parts.end(); ++li)
    {
    size_t genexPos = cmGeneratorExpression::Find(*li);
    if (genexPos == 0)
      {
      continue;
      }
    cmake::MessageType messageType = cmake::FATAL_ERROR;
    cmOStringStream e;
    if (genexPos != std::string::npos)
      {
      switch (target->GetPolicyStatusCMP0041())
        {
        case cmPolicies::WARN:
          messageType = cmake::WARNING;
          e << target->GetMakefile()->GetPolicies()
                      ->GetPolicyWarning(cmPolicies::CMP0041) << "\n";
          break;
        case cmPolicies::OLD:
          continue;
        case cmPolicies::REQUIRED_IF_USED:
        case cmPolicies::REQUIRED_ALWAYS:
        case cmPolicies::NEW:
          hadFatalError = true;
          break; // Issue fatal message.
        }
      }
    if (cmHasLiteralPrefix(li->c_str(), "${_IMPORT_PREFIX}"))
      {
      continue;
      }
    if (!cmSystemTools::FileIsFullPath(li->c_str()))
      {
      e << "Target \"" << target->GetName() << "\" "
           "INTERFACE_INCLUDE_DIRECTORIES property contains relative path:\n"
           "  \"" << *li << "\"";
      target->GetMakefile()->IssueMessage(messageType, e.str());
      }
    if (isSubDirectory(li->c_str(), installDir)
        && isSubDirectory(installDir, topBinaryDir))
      {
      continue;
      }
    if (isSubDirectory(li->c_str(), topBinaryDir))
      {
      e << "Target \"" << target->GetName() << "\" "
           "INTERFACE_INCLUDE_DIRECTORIES property contains path:\n"
           "  \"" << *li << "\"\nwhich is prefixed in the build directory.";
      target->GetMakefile()->IssueMessage(messageType, e.str());
      }
    if (!inSourceBuild)
      {
      if (isSubDirectory(li->c_str(), topSourceDir))
        {
        e << "Target \"" << target->GetName() << "\" "
            "INTERFACE_INCLUDE_DIRECTORIES property contains path:\n"
            "  \"" << *li << "\"\nwhich is prefixed in the source directory.";
        target->GetMakefile()->IssueMessage(messageType, e.str());
        }
      }
    }
  return !hadFatalError;
}

//----------------------------------------------------------------------------
static void prefixItems(std::string &exportDirs)
{
  std::vector<std::string> entries;
  cmGeneratorExpression::Split(exportDirs, entries);
  exportDirs = "";
  const char *sep = "";
  for(std::vector<std::string>::const_iterator ei = entries.begin();
      ei != entries.end(); ++ei)
    {
    exportDirs += sep;
    sep = ";";
    if (!cmSystemTools::FileIsFullPath(ei->c_str())
        && ei->find("${_IMPORT_PREFIX}") == std::string::npos)
      {
      exportDirs += "${_IMPORT_PREFIX}/";
      }
    exportDirs += *ei;
    }
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::PopulateIncludeDirectoriesInterface(
                      cmTargetExport *tei,
                      cmGeneratorExpression::PreprocessContext preprocessRule,
                      ImportPropertyMap &properties,
                      std::vector<std::string> &missingTargets)
{
  cmTarget *target = tei->Target;
  assert(preprocessRule == cmGeneratorExpression::InstallInterface);

  const char *propName = "INTERFACE_INCLUDE_DIRECTORIES";
  const char *input = target->GetProperty(propName);

  cmListFileBacktrace lfbt;
  cmGeneratorExpression ge(lfbt);

  std::string dirs = cmGeneratorExpression::Preprocess(
                                            tei->InterfaceIncludeDirectories,
                                            preprocessRule,
                                            true);
  this->ReplaceInstallPrefix(dirs);
  cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(dirs);
  std::string exportDirs = cge->Evaluate(target->GetMakefile(), "",
                                         false, target);

  if (cge->GetHadContextSensitiveCondition())
    {
    cmMakefile* mf = target->GetMakefile();
    cmOStringStream e;
    e << "Target \"" << target->GetName() << "\" is installed with "
    "INCLUDES DESTINATION set to a context sensitive path.  Paths which "
    "depend on the configuration, policy values or the link interface are "
    "not supported.  Consider using target_include_directories instead.";
    mf->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }

  if (!input && exportDirs.empty())
    {
    return;
    }
  if ((input && !*input) && exportDirs.empty())
    {
    // Set to empty
    properties[propName] = "";
    return;
    }

  prefixItems(exportDirs);

  std::string includes = (input?input:"");
  const char* sep = input ? ";" : "";
  includes += sep + exportDirs;
  std::string prepro = cmGeneratorExpression::Preprocess(includes,
                                                         preprocessRule,
                                                         true);
  if (!prepro.empty())
    {
    this->ResolveTargetsInGeneratorExpressions(prepro, target,
                                                missingTargets);

    if (!checkInterfaceDirs(prepro, target))
      {
      return;
      }
    properties[propName] = prepro;
    }
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::PopulateInterfaceProperty(
                      const std::string& propName,
                      cmTarget *target,
                      cmGeneratorExpression::PreprocessContext preprocessRule,
                      ImportPropertyMap &properties,
                      std::vector<std::string> &missingTargets)
{
  this->PopulateInterfaceProperty(propName, propName, target, preprocessRule,
                            properties, missingTargets);
}


//----------------------------------------------------------------------------
void getPropertyContents(cmTarget const* tgt, const std::string& prop,
         std::set<std::string> &ifaceProperties)
{
  const char *p = tgt->GetProperty(prop);
  if (!p)
    {
    return;
    }
  std::vector<std::string> content;
  cmSystemTools::ExpandListArgument(p, content);
  for (std::vector<std::string>::const_iterator ci = content.begin();
    ci != content.end(); ++ci)
    {
    ifaceProperties.insert(*ci);
    }
}

//----------------------------------------------------------------------------
void getCompatibleInterfaceProperties(cmTarget *target,
                                      std::set<std::string> &ifaceProperties,
                                      const std::string& config)
{
  cmComputeLinkInformation *info = target->GetLinkInformation(config);

  if (!info)
    {
    cmMakefile* mf = target->GetMakefile();
    cmOStringStream e;
    e << "Exporting the target \"" << target->GetName() << "\" is not "
        "allowed since its linker language cannot be determined";
    mf->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }

  const cmComputeLinkInformation::ItemVector &deps = info->GetItems();

  for(cmComputeLinkInformation::ItemVector::const_iterator li =
      deps.begin();
      li != deps.end(); ++li)
    {
    if (!li->Target)
      {
      continue;
      }
    getPropertyContents(li->Target,
                        "COMPATIBLE_INTERFACE_BOOL",
                        ifaceProperties);
    getPropertyContents(li->Target,
                        "COMPATIBLE_INTERFACE_STRING",
                        ifaceProperties);
    getPropertyContents(li->Target,
                        "COMPATIBLE_INTERFACE_NUMBER_MIN",
                        ifaceProperties);
    getPropertyContents(li->Target,
                        "COMPATIBLE_INTERFACE_NUMBER_MAX",
                        ifaceProperties);
    }
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::PopulateCompatibleInterfaceProperties(
                                cmTarget *target,
                                ImportPropertyMap &properties)
{
  this->PopulateInterfaceProperty("COMPATIBLE_INTERFACE_BOOL",
                                target, properties);
  this->PopulateInterfaceProperty("COMPATIBLE_INTERFACE_STRING",
                                target, properties);
  this->PopulateInterfaceProperty("COMPATIBLE_INTERFACE_NUMBER_MIN",
                                target, properties);
  this->PopulateInterfaceProperty("COMPATIBLE_INTERFACE_NUMBER_MAX",
                                target, properties);

  std::set<std::string> ifaceProperties;

  getPropertyContents(target, "COMPATIBLE_INTERFACE_BOOL", ifaceProperties);
  getPropertyContents(target, "COMPATIBLE_INTERFACE_STRING", ifaceProperties);
  getPropertyContents(target, "COMPATIBLE_INTERFACE_NUMBER_MIN",
                      ifaceProperties);
  getPropertyContents(target, "COMPATIBLE_INTERFACE_NUMBER_MAX",
                      ifaceProperties);

  if (target->GetType() != cmTarget::INTERFACE_LIBRARY)
    {
    getCompatibleInterfaceProperties(target, ifaceProperties, "");

    std::vector<std::string> configNames;
    target->GetMakefile()->GetConfigurations(configNames);

    for (std::vector<std::string>::const_iterator ci = configNames.begin();
      ci != configNames.end(); ++ci)
      {
      getCompatibleInterfaceProperties(target, ifaceProperties, *ci);
      }
    }

  for (std::set<std::string>::const_iterator it = ifaceProperties.begin();
    it != ifaceProperties.end(); ++it)
    {
    this->PopulateInterfaceProperty("INTERFACE_" + *it,
                                    target, properties);
    }
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::GenerateInterfaceProperties(cmTarget const* target,
                                        std::ostream& os,
                                        const ImportPropertyMap &properties)
{
  if (!properties.empty())
    {
    std::string targetName = this->Namespace;
    targetName += target->GetExportName();
    os << "set_target_properties(" << targetName << " PROPERTIES\n";
    for(ImportPropertyMap::const_iterator pi = properties.begin();
        pi != properties.end(); ++pi)
      {
      os << "  " << pi->first << " \"" << pi->second << "\"\n";
      }
    os << ")\n\n";
    }
}

//----------------------------------------------------------------------------
bool
cmExportFileGenerator::AddTargetNamespace(std::string &input,
                                    cmTarget* target,
                                    std::vector<std::string> &missingTargets)
{
  cmMakefile *mf = target->GetMakefile();

  cmTarget *tgt = mf->FindTargetToUse(input);
  if (!tgt)
    {
    return false;
    }

  if(tgt->IsImported())
    {
    return true;
    }
  if(this->ExportedTargets.find(tgt) != this->ExportedTargets.end())
    {
    input = this->Namespace + tgt->GetExportName();
    }
  else
    {
    std::string namespacedTarget;
    this->HandleMissingTarget(namespacedTarget, missingTargets,
                              mf, target, tgt);
    if (!namespacedTarget.empty())
      {
      input = namespacedTarget;
      }
    }
  return true;
}

//----------------------------------------------------------------------------
void
cmExportFileGenerator::ResolveTargetsInGeneratorExpressions(
                                    std::string &input,
                                    cmTarget* target,
                                    std::vector<std::string> &missingTargets,
                                    FreeTargetsReplace replace)
{
  if (replace == NoReplaceFreeTargets)
    {
    this->ResolveTargetsInGeneratorExpression(input, target, missingTargets);
    return;
    }
  std::vector<std::string> parts;
  cmGeneratorExpression::Split(input, parts);

  std::string sep;
  input = "";
  for(std::vector<std::string>::iterator li = parts.begin();
      li != parts.end(); ++li)
    {
    if (cmGeneratorExpression::Find(*li) == std::string::npos)
      {
      this->AddTargetNamespace(*li, target, missingTargets);
      }
    else
      {
      this->ResolveTargetsInGeneratorExpression(
                                    *li,
                                    target,
                                    missingTargets);
      }
    input += sep + *li;
    sep = ";";
    }
}

//----------------------------------------------------------------------------
void
cmExportFileGenerator::ResolveTargetsInGeneratorExpression(
                                    std::string &input,
                                    cmTarget* target,
                                    std::vector<std::string> &missingTargets)
{
  std::string::size_type pos = 0;
  std::string::size_type lastPos = pos;

  cmMakefile *mf = target->GetMakefile();

  while((pos = input.find("$<TARGET_PROPERTY:", lastPos)) != input.npos)
    {
    std::string::size_type nameStartPos = pos +
                                            sizeof("$<TARGET_PROPERTY:") - 1;
    std::string::size_type closePos = input.find(">", nameStartPos);
    std::string::size_type commaPos = input.find(",", nameStartPos);
    std::string::size_type nextOpenPos = input.find("$<", nameStartPos);
    if (commaPos == input.npos // Implied 'this' target
        || closePos == input.npos // Imcomplete expression.
        || closePos < commaPos // Implied 'this' target
        || nextOpenPos < commaPos) // Non-literal
      {
      lastPos = nameStartPos;
      continue;
      }

    std::string targetName = input.substr(nameStartPos,
                                                commaPos - nameStartPos);

    if (this->AddTargetNamespace(targetName, target, missingTargets))
      {
      input.replace(nameStartPos, commaPos - nameStartPos, targetName);
      }
    lastPos = nameStartPos + targetName.size() + 1;
    }

  std::string errorString;
  pos = 0;
  lastPos = pos;
  while((pos = input.find("$<TARGET_NAME:", lastPos)) != input.npos)
    {
    std::string::size_type nameStartPos = pos + sizeof("$<TARGET_NAME:") - 1;
    std::string::size_type endPos = input.find(">", nameStartPos);
    if (endPos == input.npos)
      {
      errorString = "$<TARGET_NAME:...> expression incomplete";
      break;
      }
    std::string targetName = input.substr(nameStartPos,
                                                endPos - nameStartPos);
    if(targetName.find("$<") != input.npos)
      {
      errorString = "$<TARGET_NAME:...> requires its parameter to be a "
                    "literal.";
      break;
      }
    if (!this->AddTargetNamespace(targetName, target, missingTargets))
      {
      errorString = "$<TARGET_NAME:...> requires its parameter to be a "
                    "reachable target.";
      break;
      }
    input.replace(pos, endPos - pos + 1, targetName);
    lastPos = endPos;
    }

  this->ReplaceInstallPrefix(input);

  if (!errorString.empty())
    {
    mf->IssueMessage(cmake::FATAL_ERROR, errorString);
    }
}

//----------------------------------------------------------------------------
void
cmExportFileGenerator::ReplaceInstallPrefix(std::string &)
{
  // Do nothing
}

//----------------------------------------------------------------------------
void
cmExportFileGenerator
::SetImportLinkInterface(const std::string& config, std::string const& suffix,
                    cmGeneratorExpression::PreprocessContext preprocessRule,
                    cmTarget* target, ImportPropertyMap& properties,
                    std::vector<std::string>& missingTargets)
{
  // Add the transitive link dependencies for this configuration.
  cmTarget::LinkInterface const* iface = target->GetLinkInterface(config,
                                                                  target);
  if (!iface)
    {
    return;
    }

  if (iface->ImplementationIsInterface)
    {
    // Policy CMP0022 must not be NEW.
    this->SetImportLinkProperty(suffix, target,
                                "IMPORTED_LINK_INTERFACE_LIBRARIES",
                                iface->Libraries, properties, missingTargets);
    return;
    }

  const char *propContent;

  if (const char *prop_suffixed = target->GetProperty(
                    "LINK_INTERFACE_LIBRARIES" + suffix))
    {
    propContent = prop_suffixed;
    }
  else if (const char *prop = target->GetProperty(
                    "LINK_INTERFACE_LIBRARIES"))
    {
    propContent = prop;
    }
  else
    {
    return;
    }

  const bool newCMP0022Behavior =
                        target->GetPolicyStatusCMP0022() != cmPolicies::WARN
                     && target->GetPolicyStatusCMP0022() != cmPolicies::OLD;

  if(newCMP0022Behavior && !this->ExportOld)
    {
    cmMakefile *mf = target->GetMakefile();
    cmOStringStream e;
    e << "Target \"" << target->GetName() << "\" has policy CMP0022 enabled, "
         "but also has old-style LINK_INTERFACE_LIBRARIES properties "
         "populated, but it was exported without the "
         "EXPORT_LINK_INTERFACE_LIBRARIES to export the old-style properties";
    mf->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }

  if (!*propContent)
    {
    properties["IMPORTED_LINK_INTERFACE_LIBRARIES" + suffix] = "";
    return;
    }

  std::string prepro = cmGeneratorExpression::Preprocess(propContent,
                                                         preprocessRule);
  if (!prepro.empty())
    {
    this->ResolveTargetsInGeneratorExpressions(prepro, target,
                                               missingTargets,
                                               ReplaceFreeTargets);
    properties["IMPORTED_LINK_INTERFACE_LIBRARIES" + suffix] = prepro;
    }
}

//----------------------------------------------------------------------------
void
cmExportFileGenerator
::SetImportDetailProperties(const std::string& config,
                            std::string const& suffix,
                            cmTarget* target, ImportPropertyMap& properties,
                            std::vector<std::string>& missingTargets
                           )
{
  // Get the makefile in which to lookup target information.
  cmMakefile* mf = target->GetMakefile();

  // Add the soname for unix shared libraries.
  if(target->GetType() == cmTarget::SHARED_LIBRARY ||
     target->GetType() == cmTarget::MODULE_LIBRARY)
    {
    // Check whether this is a DLL platform.
    bool dll_platform =
      (mf->IsOn("WIN32") || mf->IsOn("CYGWIN") || mf->IsOn("MINGW"));
    if(!dll_platform)
      {
      std::string prop;
      std::string value;
      if(target->HasSOName(config))
        {
        if(mf->IsOn("CMAKE_PLATFORM_HAS_INSTALLNAME"))
          {
          value = this->InstallNameDir(target, config);
          }
        prop = "IMPORTED_SONAME";
        value += target->GetSOName(config);
        }
      else
        {
        prop = "IMPORTED_NO_SONAME";
        value = "TRUE";
        }
      prop += suffix;
      properties[prop] = value;
      }
    }

  // Add the transitive link dependencies for this configuration.
  if(cmTarget::LinkInterface const* iface = target->GetLinkInterface(config,
                                                                     target))
    {
    this->SetImportLinkProperty(suffix, target,
                                "IMPORTED_LINK_INTERFACE_LANGUAGES",
                                iface->Languages, properties, missingTargets);

    std::vector<std::string> dummy;
    this->SetImportLinkProperty(suffix, target,
                                "IMPORTED_LINK_DEPENDENT_LIBRARIES",
                                iface->SharedDeps, properties, dummy);
    if(iface->Multiplicity > 0)
      {
      std::string prop = "IMPORTED_LINK_INTERFACE_MULTIPLICITY";
      prop += suffix;
      cmOStringStream m;
      m << iface->Multiplicity;
      properties[prop] = m.str();
      }
    }
}

//----------------------------------------------------------------------------
void
cmExportFileGenerator
::SetImportLinkProperty(std::string const& suffix,
                        cmTarget* target,
                        const std::string& propName,
                        std::vector<std::string> const& entries,
                        ImportPropertyMap& properties,
                        std::vector<std::string>& missingTargets
                       )
{
  // Skip the property if there are no entries.
  if(entries.empty())
    {
    return;
    }

  // Construct the property value.
  std::string link_entries;
  const char* sep = "";
  for(std::vector<std::string>::const_iterator li = entries.begin();
      li != entries.end(); ++li)
    {
    // Separate this from the previous entry.
    link_entries += sep;
    sep = ";";

    std::string temp = *li;
    this->AddTargetNamespace(temp, target, missingTargets);
    link_entries += temp;
    }

  // Store the property.
  std::string prop = propName;
  prop += suffix;
  properties[prop] = link_entries;
}


//----------------------------------------------------------------------------
void cmExportFileGenerator::GenerateImportHeaderCode(std::ostream& os,
                                                    const std::string& config)
{
  os << "#----------------------------------------------------------------\n"
     << "# Generated CMake target import file";
  if(!config.empty())
    {
    os << " for configuration \"" << config << "\".\n";
    }
  else
    {
    os << ".\n";
    }
  os << "#----------------------------------------------------------------\n"
     << "\n";
  this->GenerateImportVersionCode(os);
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::GenerateImportFooterCode(std::ostream& os)
{
  os << "# Commands beyond this point should not need to know the version.\n"
     << "set(CMAKE_IMPORT_FILE_VERSION)\n";
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::GenerateImportVersionCode(std::ostream& os)
{
  // Store an import file format version.  This will let us change the
  // format later while still allowing old import files to work.
  os << "# Commands may need to know the format version.\n"
     << "set(CMAKE_IMPORT_FILE_VERSION 1)\n"
     << "\n";
}

//----------------------------------------------------------------------------
void cmExportFileGenerator::GenerateExpectedTargetsCode(std::ostream& os,
                                            const std::string &expectedTargets)
{
  os << "# Protect against multiple inclusion, which would fail when already "
        "imported targets are added once more.\n"
        "set(_targetsDefined)\n"
        "set(_targetsNotDefined)\n"
        "set(_expectedTargets)\n"
        "foreach(_expectedTarget " << expectedTargets << ")\n"
        "  list(APPEND _expectedTargets ${_expectedTarget})\n"
        "  if(NOT TARGET ${_expectedTarget})\n"
        "    list(APPEND _targetsNotDefined ${_expectedTarget})\n"
        "  endif()\n"
        "  if(TARGET ${_expectedTarget})\n"
        "    list(APPEND _targetsDefined ${_expectedTarget})\n"
        "  endif()\n"
        "endforeach()\n"
        "if(\"${_targetsDefined}\" STREQUAL \"${_expectedTargets}\")\n"
        "  set(CMAKE_IMPORT_FILE_VERSION)\n"
        "  cmake_policy(POP)\n"
        "  return()\n"
        "endif()\n"
        "if(NOT \"${_targetsDefined}\" STREQUAL \"\")\n"
        "  message(FATAL_ERROR \"Some (but not all) targets in this export "
        "set were already defined.\\nTargets Defined: ${_targetsDefined}\\n"
        "Targets not yet defined: ${_targetsNotDefined}\\n\")\n"
        "endif()\n"
        "unset(_targetsDefined)\n"
        "unset(_targetsNotDefined)\n"
        "unset(_expectedTargets)\n"
        "\n\n";
}
//----------------------------------------------------------------------------
void
cmExportFileGenerator
::GenerateImportTargetCode(std::ostream& os, cmTarget const* target)
{
  // Construct the imported target name.
  std::string targetName = this->Namespace;

  targetName += target->GetExportName();

  // Create the imported target.
  os << "# Create imported target " << targetName << "\n";
  switch(target->GetType())
    {
    case cmTarget::EXECUTABLE:
      os << "add_executable(" << targetName << " IMPORTED)\n";
      break;
    case cmTarget::STATIC_LIBRARY:
      os << "add_library(" << targetName << " STATIC IMPORTED)\n";
      break;
    case cmTarget::SHARED_LIBRARY:
      os << "add_library(" << targetName << " SHARED IMPORTED)\n";
      break;
    case cmTarget::MODULE_LIBRARY:
      os << "add_library(" << targetName << " MODULE IMPORTED)\n";
      break;
    case cmTarget::UNKNOWN_LIBRARY:
      os << "add_library(" << targetName << " UNKNOWN IMPORTED)\n";
      break;
    case cmTarget::INTERFACE_LIBRARY:
      os << "add_library(" << targetName << " INTERFACE IMPORTED)\n";
      break;
    default:  // should never happen
      break;
    }

  // Mark the imported executable if it has exports.
  if(target->IsExecutableWithExports())
    {
    os << "set_property(TARGET " << targetName
       << " PROPERTY ENABLE_EXPORTS 1)\n";
    }

  // Mark the imported library if it is a framework.
  if(target->IsFrameworkOnApple())
    {
    os << "set_property(TARGET " << targetName
       << " PROPERTY FRAMEWORK 1)\n";
    }

  // Mark the imported executable if it is an application bundle.
  if(target->IsAppBundleOnApple())
    {
    os << "set_property(TARGET " << targetName
       << " PROPERTY MACOSX_BUNDLE 1)\n";
    }

  if (target->IsCFBundleOnApple())
    {
    os << "set_property(TARGET " << targetName
       << " PROPERTY BUNDLE 1)\n";
    }
  os << "\n";
}

//----------------------------------------------------------------------------
void
cmExportFileGenerator
::GenerateImportPropertyCode(std::ostream& os, const std::string& config,
                             cmTarget const* target,
                             ImportPropertyMap const& properties)
{
  // Construct the imported target name.
  std::string targetName = this->Namespace;

  targetName += target->GetExportName();

  // Set the import properties.
  os << "# Import target \"" << targetName << "\" for configuration \""
     << config << "\"\n";
  os << "set_property(TARGET " << targetName
     << " APPEND PROPERTY IMPORTED_CONFIGURATIONS ";
  if(!config.empty())
    {
    os << cmSystemTools::UpperCase(config);
    }
  else
    {
    os << "NOCONFIG";
    }
  os << ")\n";
  os << "set_target_properties(" << targetName << " PROPERTIES\n";
  for(ImportPropertyMap::const_iterator pi = properties.begin();
      pi != properties.end(); ++pi)
    {
    os << "  " << pi->first << " \"" << pi->second << "\"\n";
    }
  os << "  )\n"
     << "\n";
}


//----------------------------------------------------------------------------
void cmExportFileGenerator::GenerateMissingTargetsCheckCode(std::ostream& os,
                                const std::vector<std::string>& missingTargets)
{
  if (missingTargets.empty())
    {
    os << "# This file does not depend on other imported targets which have\n"
          "# been exported from the same project but in a separate "
            "export set.\n\n";
    return;
    }
  os << "# Make sure the targets which have been exported in some other \n"
        "# export set exist.\n"
        "unset(${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE_targets)\n"
        "foreach(_target ";
  std::set<std::string> emitted;
  for(unsigned int i=0; i<missingTargets.size(); ++i)
    {
    if (emitted.insert(missingTargets[i]).second)
      {
      os << "\"" << missingTargets[i] <<  "\" ";
      }
    }
  os << ")\n"
        "  if(NOT TARGET \"${_target}\" )\n"
        "    set(${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE_targets \""
        "${${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE_targets} ${_target}\")"
        "\n"
        "  endif()\n"
        "endforeach()\n"
        "\n"
        "if(DEFINED ${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE_targets)\n"
        "  if(CMAKE_FIND_PACKAGE_NAME)\n"
        "    set( ${CMAKE_FIND_PACKAGE_NAME}_FOUND FALSE)\n"
        "    set( ${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE "
        "\"The following imported targets are "
        "referenced, but are missing: "
                 "${${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE_targets}\")\n"
        "  else()\n"
        "    message(FATAL_ERROR \"The following imported targets are "
        "referenced, but are missing: "
                "${${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE_targets}\")\n"
        "  endif()\n"
        "endif()\n"
        "unset(${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE_targets)\n"
        "\n";
}


//----------------------------------------------------------------------------
void
cmExportFileGenerator::GenerateImportedFileCheckLoop(std::ostream& os)
{
  // Add code which verifies at cmake time that the file which is being
  // imported actually exists on disk. This should in theory always be theory
  // case, but still when packages are split into normal and development
  // packages this might get broken (e.g. the Config.cmake could be part of
  // the non-development package, something similar happened to me without
  // on SUSE with a mysql pkg-config file, which claimed everything is fine,
  // but the development package was not installed.).
  os << "# Loop over all imported files and verify that they actually exist\n"
        "foreach(target ${_IMPORT_CHECK_TARGETS} )\n"
        "  foreach(file ${_IMPORT_CHECK_FILES_FOR_${target}} )\n"
        "    if(NOT EXISTS \"${file}\" )\n"
        "      message(FATAL_ERROR \"The imported target \\\"${target}\\\""
        " references the file\n"
        "   \\\"${file}\\\"\n"
        "but this file does not exist.  Possible reasons include:\n"
        "* The file was deleted, renamed, or moved to another location.\n"
        "* An install or uninstall procedure did not complete successfully.\n"
        "* The installation package was faulty and contained\n"
        "   \\\"${CMAKE_CURRENT_LIST_FILE}\\\"\n"
        "but not all the files it references.\n"
        "\")\n"
        "    endif()\n"
        "  endforeach()\n"
        "  unset(_IMPORT_CHECK_FILES_FOR_${target})\n"
        "endforeach()\n"
        "unset(_IMPORT_CHECK_TARGETS)\n"
        "\n";
}


//----------------------------------------------------------------------------
void
cmExportFileGenerator
::GenerateImportedFileChecksCode(std::ostream& os, cmTarget* target,
                                 ImportPropertyMap const& properties,
                                const std::set<std::string>& importedLocations)
{
  // Construct the imported target name.
  std::string targetName = this->Namespace;
  targetName += target->GetExportName();

  os << "list(APPEND _IMPORT_CHECK_TARGETS " << targetName << " )\n"
        "list(APPEND _IMPORT_CHECK_FILES_FOR_" << targetName << " ";

  for(std::set<std::string>::const_iterator li = importedLocations.begin();
      li != importedLocations.end();
      ++li)
    {
    ImportPropertyMap::const_iterator pi = properties.find(*li);
    if (pi != properties.end())
      {
      os << "\"" << pi->second << "\" ";
      }
    }

  os << ")\n\n";
}
