#include "core.h"
#include "stdio.h"

int main() { 
	printf("test compiled at %s\n", __TIME__);
	printf("core compiled at %s\n", coreGetBuildTime());
	return 0;
}
