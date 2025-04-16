#include <windows.h>
#include <CommCtrl.h>
#include <iostream>
#include <git2.h>

#include <locale>
#include <codecvt>
#include <string>
#include <vector>
#include <stdexcept>

#include <format>
#include <filesystem>
#include <io.h>
#include <thread>
#include <fstream>
#include <functional>

class admin_rights_exception : std::exception {};
class unhandled_error : std::exception {};
struct null_streambuf : public std::streambuf
{
   using int_type = std::streambuf::int_type;
   using traits = std::streambuf::traits_type;

   virtual int_type overflow(int_type value) override
   {
      return value;
   }
};

static int error;

static bool isAdminRightsRequired = false;

bool isRunningAdm = false;


static const char* releaseFileUrls[] = {
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/b7a8d56f-9f7d-4dbf-87b0-7c939ecd7194/download",
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/a7112311-ed44-4c1d-9c63-95dbc92b9bdd/download",
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/b793de95-0abd-426e-95a6-7757d549dc8e/download",
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/bd03c1c1-def1-4e56-be61-72b59e5dad96/download",
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/0631bf93-ede4-4bb5-8f22-0b21cae989cd/download",
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/80251363-41b4-403b-9690-7e36c4752698/download",
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/c2c8a6a0-4ca2-46c9-9e44-8844e9a937ef/download",
   "https://gitflic.ru/project/prime-world-classic/prime-world-classic-game/release/c24c9dd1-ac10-4045-8773-7083e4a70d80/8c0b504f-e2b0-458d-a48a-0754c3c38135/download",
   };

static const char* releaseFileUrlsMirrors[] = {
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/data01.pile",
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/data02.pile",
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/data03.pile",
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/data04.pile",
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/data05.pile",
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/data06.pile",
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/Asks_RU.fsb",
   "https://github.com/Prime-World-Classic/Prime-World/releases/download/2.0/Music.fsb",
};

static const char* releaseFilePaths[] = {
   "..\\Game\\Packs\\data01.pile",
   "..\\Game\\Packs\\data02.pile",
   "..\\Game\\Packs\\data03.pile",
   "..\\Game\\Packs\\data04.pile",
   "..\\Game\\Packs\\data05.pile",
   "..\\Game\\Packs\\data06.pile",
   "..\\Game\\Data\\Audio\\Asks_RU.fsb",
   "..\\Game\\Data\\Audio\\Music.fsb"
};

static const char* releaseFileHashes[] = {
   "..\\Game\\Hashes\\data01.pile.md5",
   "..\\Game\\Hashes\\data02.pile.md5",
   "..\\Game\\Hashes\\data03.pile.md5",
   "..\\Game\\Hashes\\data04.pile.md5",
   "..\\Game\\Hashes\\data05.pile.md5",
   "..\\Game\\Hashes\\data06.pile.md5",
   "..\\Game\\Hashes\\Asks_RU.fsb.md5",
   "..\\Game\\Hashes\\Music.fsb.md5"
};

#ifdef ADMIN_MANIFEST
class kill_admin_process_exception : std::exception {};
static std::atomic<bool> killAdminProcess;
#endif

static LPSTR progressBarHandle = nullptr;

int SocketListen(std::atomic<bool>& doWork);
int SocketTrancieve(const char* argv, std::atomic<bool>& doWork);
void TransmitMessage(char const* strbuf, std::streamsize strSize);
void DownloadRelease(const std::string& fileUrl, const std::string& filePath, const std::string& md5Path, int r);
bool CheckFileMD5Hash(const std::string& filename, const std::string& md5FileName);


void ThrowIfNotWithAdminRights() {
#ifndef ADMIN_MANIFEST
   if (isAdminRightsRequired) {
      throw admin_rights_exception();
   }
#endif
}


void TestKillAdminProcess() {
#ifdef ADMIN_MANIFEST
   if (killAdminProcess.load()) {
      throw kill_admin_process_exception();
   }
#endif
}


void CheckError(const char* stage)
{
   if (error < 0) {
      std::string errorLast = git_error_last()->message;
      std::cerr << stage << " failed: " << errorLast << std::endl;
      throw unhandled_error();
   }
}


static std::string refName;
int fetchhead_callback(const char* ref_name, const char* remote_url, const git_oid* oid, unsigned int is_merge, void* payload) {
   memcpy(payload, oid, sizeof(git_oid));

   refName = ref_name;

   return 0; // return-zero to stop iterating
}


int fetch_progress(const git_transfer_progress* stats, void* payload) {
   TestKillAdminProcess();
   if (stats->total_objects == 0) {
      return 0;
   }
   int progress = (int)max(0.f, min(100.f, (float)stats->received_objects / (float)stats->total_objects * 100.f));

   if (isRunningAdm) {
      std::stringstream strStream;
      strStream << "#{\"type\":\"bar\", \"data\":\"" << progress << "\"}" << std::endl;
      TransmitMessage(strStream.str().c_str(), strStream.str().size());
   }
   else {
      if (progressBarHandle) {
         HWND hwndProgressBar = (HWND)atoi(progressBarHandle);
         int result = PostMessage(hwndProgressBar, PBM_SETRANGE, 0, 100);
         result = PostMessage(hwndProgressBar, PBM_SETPOS, progress, 0);
      }
      // "name": "shader_pos"
      std::cout << "#{\"type\":\"bar\", \"data\":\"" << progress << "\"}" << std::endl;
   }

   return 0;
}


void CleanRepo(const char* repoPath)
{
   std::string path = repoPath;
   path += "\\.git";
   std::filesystem::remove_all(path);
}


void InitRepo(git_repository** repo, git_remote** remote, const char* repoPath, const char* remoteRepoUrl)
{
   ThrowIfNotWithAdminRights();

   error = git_repository_init(repo, repoPath, false);
   if (error) {
      CleanRepo(repoPath);

      error = git_repository_init(repo, repoPath, false);
      CheckError("Init");
   }

   error = git_remote_lookup(remote, *repo, "origin");
   if (error < 0) {
      error = git_remote_create(remote, *repo, "origin", remoteRepoUrl);
      CheckError("Add remote");
   }
}


bool NeedUpdate(git_repository* repo)
{
   // Check if any refs exist
   git_oid fetch_head_oidTest;
   error = git_repository_fetchhead_foreach(repo, fetchhead_callback, &fetch_head_oidTest);

   // Test if fetch is needed (only if not 'clone')
   if (!refName.empty()) {
      git_reference* head = nullptr;
      error = git_repository_head(&head, repo);
      CheckError("Get HEAD");
      const git_oid* local_commit = git_reference_target(head);

      git_remote* remote = nullptr;
      error = git_remote_lookup(&remote, repo, "origin");
      CheckError("Remote lookup");

      git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
      error = git_remote_connect(remote, GIT_DIRECTION_FETCH, &callbacks, NULL, NULL);
      CheckError("Connect");

      const git_remote_head** refs;
      size_t size;
      error = git_remote_ls(&refs, &size, remote);
      CheckError("Remote ls");

      error = git_remote_disconnect(remote);

      int cmmm = git_oid_cmp(local_commit, &refs[0]->oid);

      char oid[GIT_OID_SHA1_HEXSIZE + 1] = { 0 };
      git_oid_fmt(oid, &refs[1]->oid);

      char oidLocal[GIT_OID_SHA1_HEXSIZE + 1] = { 0 };
      git_oid_fmt(oidLocal, local_commit);

      if (git_oid_cmp(local_commit, &refs[0]->oid) == 0) {
         return false;
      }
   }
   ThrowIfNotWithAdminRights();
   return true;
}


void RemoteFetch(git_repository* repo, git_remote* remote, const char* remoteRepoUrl, const char* branchName)
{
   if (!NeedUpdate(repo)) {
      return;
   }

   // Search for remotes
   error = git_remote_lookup(&remote, repo, "origin");
   CheckError("Remote is invalid");

   // Fetch from remotes
   git_fetch_options fetchOpts = GIT_FETCH_OPTIONS_INIT;
   fetchOpts.callbacks.transfer_progress = fetch_progress;
   //fetchOpts.depth = 1;
   //fetchOpts.prune = GIT_FETCH_PRUNE;
   //error = git_remote_fetch(remote, nullptr, &fetchOpts, nullptr);
   git_remote_callbacks remoteCallbacks = GIT_REMOTE_CALLBACKS_INIT;
   error = git_remote_connect(remote, GIT_DIRECTION_FETCH, &remoteCallbacks, nullptr, nullptr);
   CheckError("Fetch connect");

   error = git_remote_download(remote, nullptr, &fetchOpts);
   CheckError("Fetch download");

   error = git_remote_disconnect(remote);
   CheckError("Remote disconnect");

   error = git_remote_update_tips(remote, &remoteCallbacks, fetchOpts.update_fetchhead, fetchOpts.download_tags, nullptr);
   CheckError("Remote disconnect");

   // Search for heads
   git_oid fetch_head_oid;
   error = git_repository_fetchhead_foreach(repo, fetchhead_callback, &fetch_head_oid);

   // Delete main branch first
   git_reference* mainBranch;
   error = git_repository_head(&mainBranch, repo);
   if (!error) {
      error = git_repository_set_head(repo, "refs/heads/tmp");
      CheckError("Unset head");

      error = git_branch_delete(mainBranch);
      CheckError("Branch delete");

      git_reference_free(mainBranch);
   }

   error = git_repository_set_head(repo, refName.c_str());
   CheckError("Set head");

   // Checkout current head FORCE
   git_checkout_options checkoutOpts = GIT_CHECKOUT_OPTIONS_INIT;
   checkoutOpts.checkout_strategy = GIT_CHECKOUT_FORCE;

   // Get commit from fetched origin
   git_commit* commit;
   error = git_commit_lookup(&commit, repo, &fetch_head_oid);
   CheckError("Commit lookup");

   // Create branch from it
   git_reference* branch;
   error = git_branch_create(&branch, repo, branchName, commit, true);
   CheckError("Create main brach");

   // Set head to it
   error = git_repository_set_head(repo, refName.c_str());
   CheckError("Set head");

   git_reference_free(branch);
   git_commit_free(commit);

   // Checkout head to id
   error = git_checkout_head(repo, &checkoutOpts);
   CheckError("Checkout head");

   git_remote_free(remote);
}


void ResetRepo(git_repository* repo, const char* repoPath)
{
   bool anyChanges = false;
   git_status_options statusopt = GIT_STATUS_OPTIONS_INIT;
   statusopt.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
   statusopt.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED;

   git_status_list* status;
   error = git_status_list_new(&status, repo, &statusopt);
   CheckError("Status List");

   size_t status_count = git_status_list_entrycount(status);
   for (size_t i = 0; i < status_count; ++i) {
      const git_status_entry* s = git_status_byindex(status, i);

      // Collect more states because remote fetch skipped checkout 
      if (s->status & (
         GIT_STATUS_WT_NEW | GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED |
         GIT_STATUS_WT_TYPECHANGE | GIT_STATUS_WT_RENAMED | GIT_STATUS_WT_UNREADABLE
         )) {
         ThrowIfNotWithAdminRights();
         anyChanges = true;
         break;
      }
   }

   git_status_list_free(status);

   if (anyChanges) {
      // Reset because RemoteFetch now skips force checkout
      git_object* head_obj = nullptr;
      error = git_revparse_single(&head_obj, repo, "HEAD");
      CheckError("Get Head");

      error = git_reset(repo, head_obj, GIT_RESET_HARD, nullptr);
      git_object_free(head_obj);
      CheckError("Reset");

      // Remove unversioned files
      error = git_status_list_new(&status, repo, &statusopt);
      CheckError("Status List");

      char long_path[4096];
      size_t repoPathLength = strlen(repoPath);
      memcpy(long_path, repoPath, repoPathLength + 1);
      long_path[repoPathLength] = '\\';

      status_count = git_status_list_entrycount(status);
      for (size_t i = 0; i < status_count; ++i) {
         const git_status_entry* s = git_status_byindex(status, i);
         if (s->status & GIT_STATUS_WT_NEW) {
            // This is an untracked file
            const char* path = s->index_to_workdir->old_file.path;

            memcpy(long_path + repoPathLength + 1, path, strlen(path) + 1);
            // Removing the file
            if (remove(long_path) == -1) {
               std::cerr << "Failed to remove untracked file:" << path << std::endl;
            }
         }
      }

      git_status_list_free(status);
   }
}


void UpdateRepo(const char* repoPath, const char* remoteRepoUrl, const char* branchName) {
   git_repository* repo = nullptr;
   git_remote* remote = nullptr;

   // Try to open the repository
   error = git_repository_open(&repo, repoPath);
   if (error < 0) {
      InitRepo(&repo, &remote, repoPath, remoteRepoUrl);
   }

   // Once the repository is cloned or opened, perform the rest of the
   if (repo) {
      RemoteFetch(repo, remote, remoteRepoUrl, branchName);
         
      ResetRepo(repo, repoPath);

      git_repository_free(repo);
   }
}


void LaunchAdminNanoUpdater() {
   std::filesystem::path cwd = std::filesystem::current_path();

   SHELLEXECUTEINFO ShExecInfo = { 0 };
   ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
   ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
   ShExecInfo.hwnd = NULL;
   ShExecInfo.lpVerb = "open";
   ShExecInfo.lpFile = "..\\Tools\\PW_NanoUpdaterAdm.exe";
   ShExecInfo.lpParameters = "";
   ShExecInfo.lpDirectory = cwd.string().c_str();
   ShExecInfo.nShow = SW_SHOW;
   ShExecInfo.hInstApp = NULL;

   if (ShellExecuteEx(&ShExecInfo)) {

      std::atomic<bool> doWork;
      doWork.store(true);

      std::thread readStdErr([&doWork]() {
         SocketListen(doWork);
         });

      HANDLE hProcess = ShExecInfo.hProcess;
      DWORD exitCode = STILL_ACTIVE;
      while (exitCode == STILL_ACTIVE) {
         GetExitCodeProcess(hProcess, &exitCode);
         Sleep(1000);
      }
      
      doWork.store(false);
      readStdErr.join();
      
      CloseHandle(ShExecInfo.hProcess);
   }
   else {
      std::cerr << "Failed to execute command." << std::endl;
   }
}


bool IsAdminRequired() {

   std::ofstream srrr("..\\Tools\\test");
   if (!srrr.fail()) {
      srrr.close();
      remove("..\\Tools\\test");
   }
   return srrr.fail();
}



#if 0
int main(int argc, char* argv[]) {
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
   PSTR lpCmdLine, int nCmdShow) {
   char* winCmd = GetCommandLine();
   std::vector<char*> argVector;
   int index = 0;
   bool newOption = true;


   // walk over the command line and convert it to argv
   while (winCmd[index] != 0) {
      if (winCmd[index] == L' ') {
         // terminate option string
         winCmd[index] = 0;
         newOption = true;

      }
      else {
         if (newOption) {
            argVector.push_back(&winCmd[index]);
         }
         newOption = false;
      }
      index++;
   }
   int argc = argVector.size();
   char** argv = argVector.data();
#endif

#ifdef TEST_HASHES
   std::atomic<bool> hashesValid = true;
   std::thread tests[_countof(releaseFileUrls)];

   for (int r = 0; r < _countof(releaseFileUrls); ++r) {
      tests[r] = std::thread([r, &hashesValid]() {
         if (!CheckFileMD5Hash(releaseFilePaths[r], releaseFileHashes[r])) {
            hashesValid.store(false);
         }
         });
   }

   for (int r = 0; r < _countof(releaseFileUrls); ++r) {
      tests[r].join();
   }

   if (!hashesValid.load()) {
      return 201;
   }
   return 0;
#endif

   bool skipRelease = false;
   for (int a = 0; a < argc; ++a) {
      if (std::string(argv[a]) == "inno") {
         skipRelease = true;
         if (a + 1 < argc) {
            progressBarHandle = argv[a + 1];
            HWND hwndProgressBar = (HWND)atoi(progressBarHandle);
            int result = PostMessage(hwndProgressBar, PBM_SETRANGE, 0, 100);
         }
         break;
      }
   }

#ifdef ADMIN_MANIFEST
   isRunningAdm = true;
   std::atomic<bool> doWork;
   doWork.store(true);
   killAdminProcess.store(false);

   std::thread writeStdOut([&doWork]() {
      SocketTrancieve("127.0.0.1", doWork);
      });

   const char szUniqueNamedMutex[] = "pwclassic_nano_updater_adm";
#else
   const char szUniqueNamedMutex[] = "pwclassic_nano_updater";
#endif
   HANDLE hHandle = CreateMutex(NULL, TRUE, szUniqueNamedMutex);
   if (ERROR_ALREADY_EXISTS == GetLastError())
   {
      CloseHandle(hHandle);
      return 1; // Exit program
   }
#ifdef ADMIN_MANIFEST
   std::thread mainProcessChecker([&doWork]() {
      while (doWork.load()) {
         // Check if PW_NanoUpdater.exe is still open
         HANDLE hHandle = CreateMutex(NULL, TRUE, "pwclassic_nano_updater");
         if (ERROR_ALREADY_EXISTS == GetLastError()) {
            CloseHandle(hHandle);
         } else {
            CloseHandle(hHandle);
            killAdminProcess.store(true);
            return;
         }
         Sleep(1000);
      }
      });
#endif

   isAdminRightsRequired = IsAdminRequired();
   std::ofstream errLogs;
   if (!isAdminRightsRequired) {
      errLogs.open(isRunningAdm ? "adm_upd_errors.txt" : "upd_errors.txt");
      std::cerr.rdbuf(errLogs.rdbuf());
   } else {
      std::cerr.rdbuf(new null_streambuf);
   }

   const char* repoPath[] = {
      "..\\Launcher\\content",
      "..\\Game",
   };

   const char* repoLabels[] = {
      "content",
      "game",
   };

   const char* remoteRepoUrl[] = {
      "https://gitflic.ru/project/prime-world-classic/content_pwc.git",
      "https://gitflic.ru/project/prime-world-classic/pwc-gitupdates.git",
   };

   const char* remoteMirrorUrl[] = {
      "https://gitlab.com/prime-world-classic/content.git",
      "https://gitlab.com/prime-world-classic/PWCGitUpdates.git",
   };

   const char* remoteMirrorUrl2[] = {
      "https://github.com/Prime-World-Classic/content.git",
      "https://github.com/Prime-World-Classic/PWCGitUpdates.git",
   };
   const char* branchName = "main";


   // Initialize the library
   int _error = git_libgit2_init();
   if (_error < 0) {
      std::cerr << "Failed to init libgit2 library!";
      return 0;
   }

#ifndef ADMIN_MANIFEST
   _error = git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, false);
   if (_error < 0) {
      std::cerr << "Failed to set non-admin opts for libgit2!";
      return 0;
   }
#endif

   for (int i = 0; i < _countof(repoPath); ++i) {
#ifndef NDEBUG
      break;
#endif
      try {
         if (isRunningAdm) {
            std::stringstream strStream;
            strStream << "#{\"type\":\"label\", \"data\":\"" << repoLabels[i] << "\"}" << std::endl;
            TransmitMessage(strStream.str().c_str(), strStream.str().size());
         }
         else {
            std::cout << "#{\"type\":\"label\", \"data\":\"" << repoLabels[i] << "\"}" << std::endl;
         }
         refName = "";
         UpdateRepo(repoPath[i], remoteRepoUrl[i], branchName);
      }
      catch (admin_rights_exception) {
#ifndef ADMIN_MANIFEST
         _error = git_libgit2_shutdown();
         LaunchAdminNanoUpdater();
         return 0;
#endif
      }
#ifdef ADMIN_MANIFEST
      catch (kill_admin_process_exception) {
         mainProcessChecker.join();
         exit(0);
      }
#endif
      catch (unhandled_error) {
         try {
            CleanRepo(repoPath[i]);
            refName = "";
            UpdateRepo(repoPath[i], remoteMirrorUrl[i], branchName);
         }
         catch (unhandled_error) {
            try {
               CleanRepo(repoPath[i]);
               refName = "";
               UpdateRepo(repoPath[i], remoteMirrorUrl2[i], branchName);
            }
            catch (unhandled_error) {
               throw std::exception("Failed mirrors");
            }
         }
      }
      catch (std::exception ex) {
         std::cerr << "Exception: " << ex.what();
         return 1;
      }
   }

   _error = git_libgit2_shutdown();
   if (_error < 0) {
      std::cerr << "Failed to shutdown libgit2 library!";
   }

   try {
      for (int r = 0; r < _countof(releaseFileUrls); ++r) {
         try {
            DownloadRelease(releaseFileUrls[r], releaseFilePaths[r], releaseFileHashes[r], r);
         }
         catch (unhandled_error) {
            DownloadRelease(releaseFileUrlsMirrors[r], releaseFilePaths[r], releaseFileHashes[r], r);
         }
      }
   }
   catch (admin_rights_exception) {
#ifndef ADMIN_MANIFEST
      LaunchAdminNanoUpdater();
      return 0;
#endif
   }
   catch (unhandled_error) {
      return 0;
   }
#ifdef ADMIN_MANIFEST
   catch (kill_admin_process_exception) {
      mainProcessChecker.join();
      exit(0);
   }
#endif

#ifdef ADMIN_MANIFEST
   doWork.store(false);
   writeStdOut.join();
   mainProcessChecker.join();
#endif
   return 0;
}