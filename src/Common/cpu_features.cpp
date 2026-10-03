#include "cpu_features.h"

#include <sys/types.h>
#include <sys/sysctl.h>

CPUFeaturesImpl::CPUFeaturesImpl()
{
	std::string cpuName;
	size_t size = 0;

	if (sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0) == 0 && size > 0)
	{
		std::vector<char> buffer(size);

		if (sysctlbyname("machdep.cpu.brand_string", buffer.data(), &size, nullptr, 0) == 0 && size > 0)
		{
			cpuName.assign(buffer.data());
		}
	}

	strncpy(m_cpuBrandName, cpuName.c_str(), sizeof(m_cpuBrandName) - 1);
	m_cpuBrandName[sizeof(m_cpuBrandName) - 1] = '\0';
}

std::string CPUFeaturesImpl::GetCPUName()
{
	return { m_cpuBrandName };
}

CPUFeaturesImpl g_CPUFeatures;
