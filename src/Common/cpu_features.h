#include <string>

class CPUFeaturesImpl
{
public:
	CPUFeaturesImpl();

	std::string GetCPUName(); // empty if not available
private:
	char m_cpuBrandName[0x40]{ 0 };
};

extern CPUFeaturesImpl g_CPUFeatures;
