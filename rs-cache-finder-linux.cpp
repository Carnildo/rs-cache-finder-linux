/* CLI Runescape cache finder for Linux.
 *
 * Creates a gzipped tarball of the files found.
 *
 * Copyright (c) 2024 Carnildo.
 * Licensed under the Creative Commons CC-0 license
 */

// Written in C++-17
// Compile: g++ -o rs-cache-finder-linux rs-cache-finder-linux.cpp

#include <errno.h>
#include <filesystem>
#include <getopt.h>
#include <regex>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

// Counter used to create unique, anonymous names for any directories added
// to the output tarball.  Anonymizing directory names has two benefits:
//  1. It protects user privacy
//  2. It ensures filenames are short enough to fit into a Tar metadata
//     block.
int gDirCounter = 0;

void showhelp(const char *progname, const char *message = nullptr)
{
	if(message)
	{
		printf("%s\n", message);
	}
	printf("\nUsage: %s [--help] [--verbose] [--exclude=<regex to exclude>] [--mask-path=<regex of path name to mask>] <search_path> <output_path>\n", progname);
	printf("\n");
	printf("--exclude: a ECMA regular expression matching folders to exclude from searching for cache files, usually because it contains false positives.  Can be specified multiple times.\n");
	printf("--mask-path: an ECMA regular expression matching folder names to replace with 'folder', generally because it contains sensitive information such as a username.  Can be specified multiple times.\n");
	printf("\n");
}

void printIfVerbose(int verbose, const char *format, ...)
{
	if(verbose)
	{
		va_list v;
		va_start(v, format);
		vprintf(format, v);
		va_end(v);
	}
}

// Directories to include wholesale in the archive
std::vector<std::string> cacheDirs = {
	"^.jagex_cache_32$",
	"^.file_store_32$",
	"^jagexcache$",
	"^classic$",
	"^loginapplet$",
	"^rsmap$",
	"^runescape$",
	"^cache-93423-17382-59373-28323$",
};

std::vector<std::regex> cacheDirRegexes;

// Directories to include if their parent is in cacheDirParents
std::vector<std::string> parentedCacheDirs = {
	"^live$",
	"^live_beta$",
};

std::vector<std::string> cacheDirParents = {
	"^oldschool$",
	"^runescape$",
};

std::vector<std::regex> parentedCacheDirRegexes;
std::vector<std::regex> cacheDirParentRegexes;

// Directory trees to exclude because they are known to produce false positives
std::vector<std::string> cacheExcludeDirs = {
	"^planeshift$",
};

std::vector<std::regex> cacheExcludeRegexes;

std::vector<std::string> cachePatterns = {
	"^code\\.dat$",
	"^jingle0\\.mid$",
	"^jingle1\\.mid$",
	"^jingle2\\.mid$",
	"^jingle3\\.mid$",
	"^jingle4\\.mid$",
	"^shared_game_unpacker\\.dat$",
	"^worldmap\\.dat$",

	"^1jfds",
	"^94jfj",
	"^a2155",
	"^cht3f",
	"^g34zx",
	"^k23lk",
	"^k4o2n",
	"^lam3n",
	"^mn24j",
	"^plam3",
	"^zck35",
	"^zko34",
	"^zl3kp",
	"^zn12n",
	"^24623168",
	"^37966926",
	"^236861982",
	"^929793776",
	"^60085811638",
	"^1913169001452",
	"^32993056653417",
	"^3305336302107891869",
	"^main_file_cache.",

	"\\.jag$",

	"^loader.*\\.(jar|cab|zip)$",
	"^mapview.*\\.(jar|cab|zip)$",
	"^runescape.*\\.(jar|cab|zip)$",
	"^loginapplet.*\\.(jar|cab|zip)$",
	"^jag.*\\.dll$",
	"^(entity|land|maps|sounds).*\\.mem$",
	
	"mudclient",
	"\\.jag-",
	"\\.mem-",
};

std::vector<std::regex> cacheRegexes;

std::vector<std::string> maskPaths;
std::vector<std::regex> maskPathRegexes;

// Return true if any regex matches the target string
bool searchRegexes(const std::string &target, const std::vector<std::regex> &regexes)
{
	bool result = false;
	for(auto &regex : regexes)
	{
		std::smatch results;
		if(std::regex_search(target, results, regex))
		{
			result = true;
			break;
		}
	}
	return result;
}

bool isCacheDir(const std::filesystem::path &path, [[maybe_unused]]int verbose)
{
	bool isCache = searchRegexes(path.filename().string(), cacheDirRegexes);
	if(!isCache)
	{
		if(searchRegexes(path.filename().string(), parentedCacheDirRegexes) &&
		   path.has_parent_path() &&
		   searchRegexes(path.parent_path().filename().string(), cacheDirParentRegexes))
		{
			isCache = true;
		}
	}
	return isCache;
}

// Add a file to the tarball
//
// "prefix" is a prefix to be added to the filename in the tarball
void addFileToTar(const std::filesystem::path &source, const std::string &prefix, FILE *outfile, [[maybe_unused]]int verbose)
{
	unsigned char buffer[512];
	memset(buffer, 0, sizeof(buffer));
	
	struct stat statbuf;
	if(0 == stat(source.c_str(), &statbuf))
	{
		std::string tarFilename = prefix + "/" + source.filename().string();
		memcpy(buffer, tarFilename.data(), tarFilename.length());
		memcpy(buffer+100, "0000644", 8);
		memcpy(buffer+108, "0001750", 8);
		memcpy(buffer+116, "0001750", 8);
		sprintf((char *)(buffer+124), "%011lo", statbuf.st_size);
		sprintf((char *)(buffer+136), "%011lo", statbuf.st_mtime);
		memset(buffer+148, ' ', 8);
		memcpy(buffer+257, "ustar", 6);
		memcpy(buffer+263, "00", 2);	// The "2" is correct: this field is not null-terminated
		memcpy(buffer+265, "user", 5);
		memcpy(buffer+297, "user", 5);
		
		int checksum = 0;
		for(int i = 0; i < 512; i++)
		{
			checksum += buffer[i];
		}
		sprintf((char *)(buffer+148), "%07o", checksum);
		
		FILE *infile = fopen(source.c_str(), "r");
		if(infile)
		{
			if(fwrite(buffer, 512, 1, outfile) != 1)
			{
				fprintf(stderr, "Write error %s when adding to archive\n", strerror(errno));
				throw std::runtime_error("Error writing to output file");
			}
			
			while(!feof(infile))
			{
				memset(buffer, 0, sizeof(buffer));
				int bytesRead = 0;
				bytesRead = fread(buffer, 1, 512, infile);
				if(bytesRead < 512 && !feof(infile))
				{
					fprintf(stderr, "Read error %s for file %s when adding to archive\n", strerror(errno), source.c_str());
					throw std::runtime_error("Error reading input file");
				}
				if(bytesRead > 0)
				{
					if(fwrite(buffer, 512, 1, outfile) != 1) // This is correct: the Tar file format consists of null-padded 512-byte blocks, so a short read still needs a full-block write
					{
						fprintf(stderr, "Write error %s when adding to archive\n", strerror(errno));
						throw std::runtime_error("Error writing to output file");
					}
				}
			}
			fclose(infile);
			if(fflush(outfile) != 0)
			{
				fprintf(stderr, "Error %s flushing buffer to archive\n", strerror(errno));
				throw std::runtime_error("Error writing to output file");
			}
		}
		else
		{
			fprintf(stderr, "Open error %s for file %s when adding to archive\n", strerror(errno), source.c_str());
		}
	}
	else
	{
		fprintf(stderr, "Stat error %s for file %s when adding to archive\n", strerror(errno), source.c_str());
	}
}

std::string makePrefix(const std::filesystem::path &path)
{
	char prefix[70];
	std::string folder = path.filename().string();
	if(searchRegexes(folder, maskPathRegexes))
	{
		folder = "folder";
	}
	
	if(path.has_parent_path())
	{
		std::string parent = path.parent_path().filename();
		if(searchRegexes(parent, maskPathRegexes))
		{
			parent = "folder";
		}
		snprintf(prefix, sizeof(prefix), "dir%07d/%s/%s", gDirCounter, parent.c_str(), folder.c_str());
	}
	else
	{
		snprintf(prefix, sizeof(prefix), "dir%07d/%s", gDirCounter, folder.c_str());
	}
	return prefix;
}

// Add the contents of a cache directory without recursing.  I don't know if
// the non-recursion is important or not, but it's how the Windows finder works.
void addCacheDir(const std::filesystem::path &source, FILE *outfile, int verbose)
{
	gDirCounter++;
	std::string prefix = makePrefix(source);
	try
	{
		for(auto const &item : std::filesystem::directory_iterator(source, std::filesystem::directory_options::skip_permission_denied))
		{
			try
			{
				if(item.is_regular_file())
				{
					printf("Adding file %s to archive\n", item.path().c_str());
					addFileToTar(item.path(), prefix, outfile, verbose);
				}
			}
			catch(std::filesystem::filesystem_error &e)
			{
				fprintf(stderr, "Error processing cache file %s: %s\n", e.path1().c_str(), e.what());
			}
		}
	}
	catch(std::filesystem::filesystem_error &e)
	{
		fprintf(stderr, "Error processing cache directory %s: %s\n", e.path1().c_str(), e.what());
	}
}

// Scan a directory for cache-named files.  If any are found, add to the
// tarball.
void addCacheFiles(const std::filesystem::path &source, FILE *outfile, int verbose)
{
	bool foundCacheFile = false;
	std::string prefix;
	try
	{
		for(auto const &item : std::filesystem::directory_iterator(source, std::filesystem::directory_options::skip_permission_denied))
		{
			try
			{
				if(item.is_symlink())
				{
					continue;
				}
				if(item.is_regular_file() && searchRegexes(item.path().filename().string(), cacheRegexes))
				{
					printIfVerbose(verbose, "Cache file match: %s\n", item.path().c_str());
					printf("Adding file %s to archive\n", item.path().c_str());
					if(!foundCacheFile)
					{
						gDirCounter++;
						foundCacheFile = true;
						prefix = makePrefix(source);
					}
					addFileToTar(item.path(), prefix, outfile, verbose);
				}
			}
			catch(std::filesystem::filesystem_error &e)
			{
				fprintf(stderr, "Error processing file %s: %s\n", e.path1().c_str(), e.what());
			}
		}
	}
	catch(std::filesystem::filesystem_error &e)
	{
		fprintf(stderr, "Error processing directory %s: %s\n", e.path1().c_str(), e.what());
	}
}

void scanPath(const std::filesystem::path &source, FILE *outfile, int verbose)
{
	printIfVerbose(verbose, "Scanning %s\n", source.c_str());
	try
	{
		for(auto const &dir : std::filesystem::directory_iterator(source, std::filesystem::directory_options::skip_permission_denied))
		{
			try
			{
				if(dir.is_directory())
				{
					if(dir.is_symlink())
					{
						printIfVerbose(verbose, "Skipping directory symlink %s\n", dir.path().c_str());
						continue;
					}
					else if(searchRegexes(dir.path().filename().string(), cacheExcludeRegexes))
					{
						printIfVerbose(verbose, "Excluding directory %s\n", dir.path().c_str());
						continue;
					}
					else if(isCacheDir(dir.path(), verbose))
					{
						printIfVerbose(verbose, "Cache dir found: %s\n", dir.path().c_str());
						addCacheDir(dir.path(), outfile, verbose);
					}
					else
					{
						addCacheFiles(dir.path(), outfile, verbose);
					}
					scanPath(dir.path(), outfile, verbose);
				}
			}
			catch(std::filesystem::filesystem_error &e)
			{
				fprintf(stderr, "Error processing entry %s: %s\n", e.path1().c_str(), e.what());
			}
		}
	}
	catch(std::filesystem::filesystem_error &e)
	{
		fprintf(stderr, "Error scanning directory %s: %s\n", e.path1().c_str(), e.what());
	}
}

std::vector<std::regex> CompileRegexes(const std::vector<std::string> &patterns)
{
	std::vector<std::regex> result;
	for(auto pattern : patterns)
	{
		std::regex regex(pattern, std::regex::ECMAScript|std::regex::icase|std::regex::nosubs|std::regex::optimize);
		result.push_back(regex);
	}
	return result;
}

int main(int argc, char *argv[])
{
	int help = 0;
	int verbose = 0;
	std::vector<std::string> extraExcludes;
	
	static struct option long_options[] = {
		{"verbose",	no_argument,		&verbose, 1},
		{"help",	no_argument,		&help, 1},
		{"exclude",	required_argument,	0, 0},
		{"mask-path",	required_argument,	0, 0},
		{0,		0,			0, 0}
	};
	
	while(1)
	{
		int res = -1;
		int longIndex = 0;
		res = getopt_long(argc, argv, "", long_options, &longIndex);
		if(res == -1)
		{
			break;
		}
		else if(longIndex == 2)
		{
			extraExcludes.push_back(optarg);
		}
		else if(longIndex == 3)
		{
			maskPaths.push_back(optarg);
		}
	}
	
	if(help)
	{
		showhelp(argv[0]);
		return EXIT_SUCCESS;
	}
	if(optind == argc)
	{
		showhelp(argv[0], "No search path provided");
		return EXIT_FAILURE;
	}
	if(optind == argc - 1)
	{
		showhelp(argv[0], "No output path provided");
		return EXIT_FAILURE;
	}
	
	cacheRegexes = CompileRegexes(cachePatterns);
	cacheDirRegexes = CompileRegexes(cacheDirs);
	parentedCacheDirRegexes = CompileRegexes(parentedCacheDirs);
	cacheDirParentRegexes = CompileRegexes(cacheDirParents);
	cacheExcludeRegexes = CompileRegexes(cacheExcludeDirs);
	auto extraExcludeRegexes = CompileRegexes(extraExcludes);
	cacheExcludeRegexes.insert(cacheExcludeRegexes.end(), extraExcludeRegexes.begin(), extraExcludeRegexes.end());
	maskPathRegexes = CompileRegexes(maskPaths);
	
	try
	{
		std::filesystem::path source(argv[optind]);
		if(!std::filesystem::exists(source))
		{
			fprintf(stderr, "Error: Source path %s does not exist\n", source.c_str());
			return EXIT_FAILURE;
		}
		if(!std::filesystem::is_directory(source))
		{
			fprintf(stderr, "Error: Source path %s is not a directory\n", source.c_str());
			return EXIT_FAILURE;
		}
		std::filesystem::path dest(argv[optind+1]);
		FILE *outfile = NULL;
		if(std::filesystem::exists(dest))
		{
			fprintf(stderr, "Error: Output path %s already exists\n", dest.c_str());
			return EXIT_FAILURE;
		}
		else
		{
			outfile = fopen(dest.c_str(), "w");
			if(!outfile)
			{
				fprintf(stderr, "Error %s opening output file %s\n", strerror(errno), dest.c_str());
				return EXIT_FAILURE;
			}
		}
		
		scanPath(source, outfile, verbose);
		
		fflush(outfile);
		fclose(outfile);
	}
	catch(std::exception &e)
	{
		fprintf(stderr, "Failed: %s\n", e.what());
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}