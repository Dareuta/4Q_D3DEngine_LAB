#include "TutorialApp/TutorialApp.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine, _In_ int       nCmdShow)
{

#ifdef _DEBUG
	AllocConsole(); // 콘솔 창 생성

	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);  // printf, std::cout 출력
	freopen_s(&fp, "CONOUT$", "w", stderr);  // std::cerr 출력
	freopen_s(&fp, "CONIN$", "r", stdin);    // std::cin 입력 (필요하면)
#endif

	TutorialApp App;
	return App.Run(hInstance);
}
