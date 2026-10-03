// Standalone self-test for the AES-128 implementations in aes128.cpp
// Checks the known answer vectors from FIPS-197 and NIST SP 800-38A and cross-checks the
// hardware path (ARMv8 Crypto Extensions on aarch64) against the portable software path.
// Built only when ENABLE_AES128_TEST is set. Exit code 0 means all checks passed

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string_view>
#include <vector>

// minimal subset of Common/precompiled.h required by aes128.cpp
using uint8 = uint8_t;
using uint32 = uint32_t;
using sint32 = int32_t;

inline uint32 _swapEndianU32(uint32 v)
{
	return __builtin_bswap32(v);
}

#define cemu_assert_debug(__cond) ((void)0)

#include "aes128.cpp"

static sint32 s_failCount = 0;

static std::vector<uint8> FromHex(std::string_view hex)
{
	std::vector<uint8> out;
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
	{
		auto nibble = [](char c) -> uint8 {
			if (c >= '0' && c <= '9')
				return c - '0';
			return c - 'a' + 10;
		};
		out.push_back((nibble(hex[i]) << 4) | nibble(hex[i + 1]));
	}
	return out;
}

static void Check(const char* name, const uint8* actual, const uint8* expected, size_t length)
{
	if (memcmp(actual, expected, length) == 0)
		return;
	s_failCount++;
	printf("FAIL: %s\n", name);
}

static void Check(const char* name, const std::vector<uint8>& actual, const std::vector<uint8>& expected)
{
	if (actual.size() != expected.size())
	{
		s_failCount++;
		printf("FAIL: %s (size mismatch)\n", name);
		return;
	}
	Check(name, actual.data(), expected.data(), actual.size());
}

using ECBFunc = void (*)(uint8* input, const uint8* key, uint8* output);
using CBCDecryptFunc = void (*)(uint8* output, uint8* input, uint32 length, const uint8* key, const uint8* iv);

struct Implementation
{
	const char* name;
	ECBFunc ecbEncrypt;
	ECBFunc ecbDecrypt;
	CBCDecryptFunc cbcDecrypt;
};

static void TestKnownAnswers(const Implementation& impl)
{
	printf("Known answer tests: %s\n", impl.name);
	char testName[128];
	// FIPS-197 Appendix C.1
	{
		auto key = FromHex("000102030405060708090a0b0c0d0e0f");
		auto plain = FromHex("00112233445566778899aabbccddeeff");
		auto cipher = FromHex("69c4e0d86a7b0430d8cdb78070b4c55a");
		std::vector<uint8> out(16);
		impl.ecbEncrypt(plain.data(), key.data(), out.data());
		snprintf(testName, sizeof(testName), "%s FIPS-197 C.1 encrypt", impl.name);
		Check(testName, out, cipher);
		impl.ecbDecrypt(cipher.data(), key.data(), out.data());
		snprintf(testName, sizeof(testName), "%s FIPS-197 C.1 decrypt", impl.name);
		Check(testName, out, plain);
	}
	// NIST SP 800-38A F.1.1/F.1.2 (ECB-AES128) and F.2.2 (CBC-AES128.Decrypt)
	auto key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
	auto plain = FromHex(
		"6bc1bee22e409f96e93d7e117393172a"
		"ae2d8a571e03ac9c9eb76fac45af8e51"
		"30c81c46a35ce411e5fbc1191a0a52ef"
		"f69f2445df4f9b17ad2b417be66c3710");
	auto ecbCipher = FromHex(
		"3ad77bb40d7a3660a89ecaf32466ef97"
		"f5d3d58503b9699de785895a96fdbaaf"
		"43b1cd7f598ece23881b00e3ed030688"
		"7b0c785e27e8ad3f8223207104725dd4");
	auto cbcIv = FromHex("000102030405060708090a0b0c0d0e0f");
	auto cbcCipher = FromHex(
		"7649abac8119b246cee98e9b12e9197d"
		"5086cb9b507219ee95db113a917678b2"
		"73bed6b8e3c1743b7116e69e22229516"
		"3ff1caa1681fac09120eca307586e1a7");
	for (sint32 block = 0; block < 4; block++)
	{
		uint8 out[16];
		impl.ecbEncrypt(plain.data() + block * 16, key.data(), out);
		snprintf(testName, sizeof(testName), "%s SP800-38A F.1.1 block %d", impl.name, (int)block);
		Check(testName, out, ecbCipher.data() + block * 16, 16);
		impl.ecbDecrypt(ecbCipher.data() + block * 16, key.data(), out);
		snprintf(testName, sizeof(testName), "%s SP800-38A F.1.2 block %d", impl.name, (int)block);
		Check(testName, out, plain.data() + block * 16, 16);
	}
	{
		std::vector<uint8> out(64);
		impl.cbcDecrypt(out.data(), cbcCipher.data(), 64, key.data(), cbcIv.data());
		snprintf(testName, sizeof(testName), "%s SP800-38A F.2.2", impl.name);
		Check(testName, out, plain);
		// in-place, as used by the FST/WUD code
		std::vector<uint8> inPlace = cbcCipher;
		impl.cbcDecrypt(inPlace.data(), inPlace.data(), 64, key.data(), cbcIv.data());
		snprintf(testName, sizeof(testName), "%s SP800-38A F.2.2 in-place", impl.name);
		Check(testName, inPlace, plain);
	}
}

static void TestCBCEncryptAndCTR()
{
	printf("Known answer tests: AES128_CBC_encrypt, AES128CTR_transform\n");
	auto key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
	auto plain = FromHex(
		"6bc1bee22e409f96e93d7e117393172a"
		"ae2d8a571e03ac9c9eb76fac45af8e51"
		"30c81c46a35ce411e5fbc1191a0a52ef"
		"f69f2445df4f9b17ad2b417be66c3710");
	// NIST SP 800-38A F.2.1 (CBC-AES128.Encrypt). Note: AES128_CBC_encrypt modifies its input buffer
	{
		auto iv = FromHex("000102030405060708090a0b0c0d0e0f");
		auto expected = FromHex(
			"7649abac8119b246cee98e9b12e9197d"
			"5086cb9b507219ee95db113a917678b2"
			"73bed6b8e3c1743b7116e69e22229516"
			"3ff1caa1681fac09120eca307586e1a7");
		std::vector<uint8> input = plain;
		std::vector<uint8> out(64);
		AES128_CBC_encrypt(out.data(), input.data(), 64, key.data(), iv.data());
		Check("SP800-38A F.2.1", out, expected);
	}
	// NIST SP 800-38A F.5.1 (CTR-AES128.Encrypt)
	{
		auto counter = FromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
		auto expected = FromHex(
			"874d6191b620e3261bef6864990db6ce"
			"9806f66b7970fdff8617187bb9fffdff"
			"5ae4df3edbd5d35e5b4f09020db03eab"
			"1e031dda2fbe03d1792170a0f3009cee");
		std::vector<uint8> data = plain;
		AES128CTR_transform(data.data(), 64, key.data(), counter.data());
		Check("SP800-38A F.5.1", data, expected);
	}
}

// compare an implementation against the software implementation using random data
static void TestCrossCheck(const Implementation& reference, const Implementation& impl)
{
	printf("Cross check: %s vs %s\n", impl.name, reference.name);
	char testName[128];
	std::mt19937 rng(12345);
	auto randomFill = [&](uint8* buf, size_t length) {
		for (size_t i = 0; i < length; i++)
			buf[i] = (uint8)rng();
	};
	for (sint32 i = 0; i < 1000; i++)
	{
		uint8 key[16], input[16], outRef[16], outImpl[16];
		randomFill(key, sizeof(key));
		randomFill(input, sizeof(input));
		reference.ecbEncrypt(input, key, outRef);
		impl.ecbEncrypt(input, key, outImpl);
		snprintf(testName, sizeof(testName), "ECB encrypt random #%d", (int)i);
		Check(testName, outImpl, outRef, 16);
		reference.ecbDecrypt(input, key, outRef);
		impl.ecbDecrypt(input, key, outImpl);
		snprintf(testName, sizeof(testName), "ECB decrypt random #%d", (int)i);
		Check(testName, outImpl, outRef, 16);
	}
	// cover every remainder of the 4-way interleaved loop, partial trailing blocks, null IV and in-place operation
	for (uint32 length = 1; length <= 16 * 37; length += (length < 64) ? 1 : 7)
	{
		uint32 paddedLength = (length + 15) & ~15u;
		std::vector<uint8> input(paddedLength), outRef(paddedLength), outImpl(paddedLength);
		uint8 key[16], iv[16];
		randomFill(key, sizeof(key));
		randomFill(iv, sizeof(iv));
		randomFill(input.data(), input.size());
		for (sint32 variant = 0; variant < 3; variant++)
		{
			const uint8* ivParam = (variant == 1) ? nullptr : iv;
			if (variant == 2)
			{
				outRef = input;
				outImpl = input;
				reference.cbcDecrypt(outRef.data(), outRef.data(), length, key, ivParam);
				impl.cbcDecrypt(outImpl.data(), outImpl.data(), length, key, ivParam);
			}
			else
			{
				reference.cbcDecrypt(outRef.data(), input.data(), length, key, ivParam);
				impl.cbcDecrypt(outImpl.data(), input.data(), length, key, ivParam);
			}
			snprintf(testName, sizeof(testName), "CBC decrypt length %u variant %d", length, (int)variant);
			Check(testName, outImpl, outRef);
		}
	}
}

int main()
{
	AES128_init();
	const Implementation softImpl{ "software", __soft__AES128_ECB_encrypt, __soft__AES128_ECB_decrypt, __soft__AES128_CBC_decrypt };
	const Implementation activeImpl{ "active (AES128_init)", AES128_ECB_encrypt, AES128_ECB_decrypt, AES128_CBC_decrypt };
	TestKnownAnswers(softImpl);
	TestKnownAnswers(activeImpl);
#if defined(__aarch64__)
	const Implementation armImpl{ "ARMv8 crypto", __armv8__AES128_ECB_encrypt, __armv8__AES128_ECB_decrypt, __armv8__AES128_CBC_decrypt };
	if (AES128_ECB_encrypt != __armv8__AES128_ECB_encrypt || AES128_CBC_decrypt != __armv8__AES128_CBC_decrypt)
	{
		s_failCount++;
		printf("FAIL: AES128_init() did not select the ARMv8 implementation\n");
	}
	TestKnownAnswers(armImpl);
	TestCrossCheck(softImpl, armImpl);
#endif
	TestCBCEncryptAndCTR();
	if (s_failCount != 0)
	{
		printf("%d check(s) FAILED\n", (int)s_failCount);
		return 1;
	}
	printf("All AES128 tests passed\n");
	return 0;
}
