// Independent runtime startup control: no component, database, threads or races.
#include <cstdio>
int main() {
	std::puts("TSan startup control entered main");
	return 0;
}
