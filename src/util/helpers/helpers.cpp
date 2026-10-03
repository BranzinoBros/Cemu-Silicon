#include "helpers.h"

#include <algorithm> 
#include <functional> 
#include <cctype>
#include <random>

#include "config/ActiveSettings.h"

#include <boost/random/uniform_int.hpp>

#include <zlib.h>


std::string& ltrim(std::string& str, const std::string& chars)
{
	str.erase(0, str.find_first_not_of(chars));
	return str;
}

std::string& rtrim(std::string& str, const std::string& chars)
{
	str.erase(str.find_last_not_of(chars) + 1);
	return str;
}

std::string& trim(std::string& str, const std::string& chars)
{
	return ltrim(rtrim(str, chars), chars);
}


std::string_view& ltrim(std::string_view& str, const std::string& chars)
{
	str.remove_prefix(std::min(str.find_first_not_of(chars), str.size()));
	return str;
}
std::string_view& rtrim(std::string_view& str, const std::string& chars)
{
	str.remove_suffix(std::max(str.size() - str.find_last_not_of(chars) - 1, (size_t)0));
	return str;
}
std::string_view& trim(std::string_view& str, const std::string& chars)
{
	return ltrim(rtrim(str, chars), chars);
}

std::string GetSystemErrorMessage()
{
	return "";
}

std::string GetSystemErrorMessage(const std::exception& ex)
{
	const std::string msg = GetSystemErrorMessage();
	if(msg.empty())
		return ex.what();

	return fmt::format("{}\n{}",msg, ex.what());
}

std::string GetSystemErrorMessage(const std::error_code& ec)
{
	const std::string msg = GetSystemErrorMessage();
	if(msg.empty())
		return ec.message();

	return fmt::format("{}\n{}",msg, ec.message());
}

void SetThreadName(const char* name)
{
	pthread_setname_np(name);
}

std::string ltrim_copy(const std::string& s)
{
	std::string result = s;
	ltrim(result);
	return result;
}

std::string rtrim_copy(const std::string& s)
{
	std::string result = s;
	rtrim(result);
	return result;
}

uint32_t GetPhysicalCoreCount()
{
	static uint32_t s_core_count = 0;
	if (s_core_count != 0)
		return s_core_count;
	
	return std::thread::hardware_concurrency();
}

bool TestWriteAccess(const fs::path& p)
{
	std::error_code ec;
	// must be path and must exist
	if (!fs::exists(p, ec) || !fs::is_directory(p, ec))
		return false;

	// retry 3 times
	for (int i = 0; i < 3; ++i)
	{
		const auto filename = p / fmt::format("_{}.tmp", GenerateRandomString(8));
		if (fs::exists(filename, ec))
			continue;

		std::ofstream file(filename);
		if (!file.is_open()) // file couldn't be created
			break;
		
		file.close();

		fs::remove(filename, ec);
		return true;
	}
	
	return false;
}

// make path relative to Cemu directory
fs::path MakeRelativePath(const fs::path& base, const fs::path& path)
{
	try
	{
		return fs::relative(path, base);
	}
	catch (const std::exception&)
	{
		return path;
	}
}

std::vector<std::string_view> TokenizeView(std::string_view str, char delimiter)
{
	std::vector<std::string_view> result;

	size_t last_token_index = 0;
	for (auto index = str.find(delimiter); index != std::string_view::npos; index = str.find(delimiter, index + 1))
	{
		const auto token = str.substr(last_token_index, index - last_token_index);
		result.emplace_back(token);

		last_token_index = index + 1;
	}

	try
	{
		const auto token = str.substr(last_token_index);
		result.emplace_back(token);
	}
	catch (const std::invalid_argument&) {}

	return result;
}

std::vector<std::string> Tokenize(std::string_view str, char delimiter)
{
	std::vector<std::string> result;

	size_t last_token_index = 0;
	for (auto index = str.find(delimiter); index != std::string_view::npos; index = str.find(delimiter, index + 1))
	{
		const auto token = str.substr(last_token_index, index - last_token_index);
		result.emplace_back(token);

		last_token_index = index + 1;
	}

	try
	{
		const auto token = str.substr(last_token_index);
		result.emplace_back(token);
	}
	catch (const std::invalid_argument&) {}

	return result;
}

std::string GenerateRandomString(size_t length)
{
	const std::string kCharacters{
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"1234567890" };
	return GenerateRandomString(length, kCharacters);
}

std::string GenerateRandomString(const size_t length, const std::string_view characters)
{
	assert(!characters.empty());
	std::string result;
	result.resize(length);

	std::random_device rd;
	std::mt19937 gen(rd());
     
        // workaround for static asserts using boost
        boost::random::uniform_int_distribution<decltype(characters.size())> index_dist(0, characters.size() - 1);
	std::generate_n(
		result.begin(),
		length,
		[&] { return characters[index_dist(gen)]; }
	);

	return result;
}

std::optional<std::vector<uint8>> zlibDecompress(const std::vector<uint8>& compressed, size_t sizeHint)
{
	int err;
	std::vector<uint8> decompressed;
	size_t outWritten = 0;
	size_t bytesPerIteration = sizeHint;
	z_stream stream;
	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	stream.avail_in = compressed.size();
	stream.next_in = (Bytef*)compressed.data();
	err = inflateInit2(&stream, 32); // 32 is a zlib magic value to enable header detection
	if (err != Z_OK)
		return {};

	do
	{
		decompressed.resize(decompressed.size() + bytesPerIteration);
		const auto availBefore = decompressed.size() - outWritten;
		stream.avail_out = availBefore;
		stream.next_out = decompressed.data() + outWritten;
		err = inflate(&stream, Z_NO_FLUSH);
		if (!(err == Z_OK || err == Z_STREAM_END))
		{
			inflateEnd(&stream);
			return {};
		}
		outWritten += availBefore - stream.avail_out;
		bytesPerIteration *= 2;
	}
	while (err != Z_STREAM_END);

	inflateEnd(&stream);
	decompressed.resize(stream.total_out);

	return decompressed;
}
