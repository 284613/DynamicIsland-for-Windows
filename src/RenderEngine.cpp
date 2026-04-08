//RenderEngine.cpp



#include "RenderEngine.h"



#include "Components\VolumeComponent.h"



#include "Components\MusicPlayerComponent.h"



#include "Components\AlertComponent.h"



#include <cmath>



RenderEngine::RenderEngine() : m_lastDrawnFullText(L"") {}



RenderEngine::~RenderEngine() {}











bool RenderEngine::Initialize(HWND hwnd, int canvasWidth, int canvasHeight) {



	HRESULT hr;



	// 1. 初始化 D3D / D2D / DComp (与之前一致)



	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,



		nullptr, 0, D3D11_SDK_VERSION, &m_d3dDevice, nullptr, nullptr);



	if (FAILED(hr)) return false;







	hr = m_d3dDevice.As(&m_dxgiDevice);



	if (FAILED(hr)) return false;







	hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());



	if (FAILED(hr)) return false;







	hr = m_d2dFactory->CreateDevice(m_dxgiDevice.Get(), &m_d2dDevice);



	if (FAILED(hr)) return false;







	hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);



	if (FAILED(hr)) return false;







	hr = DCompositionCreateDevice(m_dxgiDevice.Get(), __uuidof(IDCompositionDevice), (void**)&m_dcompDevice);



	if (FAILED(hr)) return false;







	hr = m_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &m_dcompTarget);



	if (FAILED(hr)) return false;







	hr = m_dcompDevice->CreateVisual(&m_rootVisual);



	if (FAILED(hr)) return false;







	hr = m_dcompDevice->CreateSurface(canvasWidth, canvasHeight, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &m_surface);



	if (FAILED(hr)) return false;







	m_rootVisual->SetContent(m_surface.Get());



	m_dcompTarget->SetRoot(m_rootVisual.Get());







	// 2. 初始化 DirectWrite



	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dwriteFactory);



	if (FAILED(hr)) return false;







	// 【找回丢失的标题字体】创建主标题字体格式 (微软雅黑, 粗体, 16px)



	hr = m_dwriteFactory->CreateTextFormat(



		L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_BOLD,



		DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-cn", &m_textFormatTitle);



	if (FAILED(hr)) return false;







	// 【找回丢失的副标题字体】创建副标题字体格式 (微软雅黑, 常规, 13px)



	hr = m_dwriteFactory->CreateTextFormat(



		L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL,



		DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-cn", &m_textFormatSub);



	if (FAILED(hr)) return false;







	// 创建控制按钮的图标字体格式 (Segoe MDL2 Assets)



	hr = m_dwriteFactory->CreateTextFormat(



		L"Segoe MDL2 Assets", nullptr,



		DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,



		18.0f, L"zh-cn", &m_iconTextFormat);



	if (FAILED(hr)) return false;







	// 设置图标文本绝对居中，确保画出来的悬浮背景恰好包围它



	m_iconTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);



	m_iconTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);







	m_d2dContext->CreateSolidColorBrush(



		D2D1::ColorF(0.08f, 0.08f, 0.1f, 0.8f),



		&m_blackBrush



	);







	m_d2dContext->CreateSolidColorBrush(



		D2D1::ColorF(1, 1, 1, 1),



		&m_whiteBrush



	);







	m_d2dContext->CreateSolidColorBrush(



		D2D1::ColorF(0.6f, 0.6f, 0.6f, 1),



		&m_grayBrush



	);







	m_d2dContext->CreateSolidColorBrush(



		D2D1::ColorF(1.0f, 0.2f, 0.4f, 1),



		&m_themeBrush



	);







	// 【新增】预创建动态颜色画刷（初始化为默认色，后续通过SetColor改变）



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.8f, 0.3f, 1.0f), &m_wifiBrush);      // WiFi 绿



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.5f, 1.0f, 1.0f), &m_bluetoothBrush); // 蓝牙 蓝



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.85f, 0.4f, 1.0f), &m_chargingBrush); // 充电 亮绿



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.2f, 0.2f, 1.0f), &m_lowBatteryBrush); // 低电量 大红



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.6f, 1.0f, 1.0f), &m_fileBrush);      // 文件暂存 天蓝



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.3f, 0.3f, 1.0f), &m_notificationBrush); // 通知 橙红



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f), &m_darkGrayBrush);   // 深灰色背景



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.8f, 0.8f, 0.8f, 0.5f), &m_progressBgBrush); // 进度条背景



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_progressFgBrush); // 进度条前景



	m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_buttonHoverBrush); // 按钮悬浮背景







	// 【新增】创建歌词遮罩渐变画刷



	if (m_d2dContext) {



		// 左侧渐变（透明 -> 不透明）



		D2D1_GRADIENT_STOP gradientStopsLeft[2] = {



			{0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)},  // 透明



			{1.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f)}   // 不透明



		};



		ComPtr<ID2D1GradientStopCollection> gradientStopsLeftCollection;



		m_d2dContext->CreateGradientStopCollection(gradientStopsLeft, 2, &gradientStopsLeftCollection);



		m_d2dContext->CreateLinearGradientBrush(



			D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(30.0f, 0.0f)),



			gradientStopsLeftCollection.Get(),



			&m_lyricFadeLeftBrush



		);







		// 右侧渐变（不透明 -> 透明）



		D2D1_GRADIENT_STOP gradientStopsRight[2] = {



			{0.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f)},   // 不透明



			{1.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)}  // 透明



		};



		ComPtr<ID2D1GradientStopCollection> gradientStopsRightCollection;



		m_d2dContext->CreateGradientStopCollection(gradientStopsRight, 2, &gradientStopsRightCollection);



		m_d2dContext->CreateLinearGradientBrush(



			D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(30.0f, 0.0f)),



			gradientStopsRightCollection.Get(),



			&m_lyricFadeRightBrush



		);



	}







	CoCreateInstance(



		CLSID_WICImagingFactory,



		nullptr,



		CLSCTX_INPROC_SERVER,



		IID_PPV_ARGS(&m_wicFactory)



	);



	ComPtr<IDXGISurface> dxgiSurface;



	POINT offset;







	m_surface->BeginDraw(nullptr, __uuidof(IDXGISurface), (void**)&dxgiSurface, &offset);







	D2D1_BITMAP_PROPERTIES1 bitmapProperties =



		D2D1::BitmapProperties1(



			D2D1_BITMAP_OPTIONS_TARGET,



			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,



				D2D1_ALPHA_MODE_PREMULTIPLIED));







	m_d2dContext->CreateBitmapFromDxgiSurface(



		dxgiSurface.Get(),



		&bitmapProperties,



		&m_targetBitmap);



	m_d2dContext->SetTarget(m_targetBitmap.Get());



	m_surface->EndDraw();



	return true;



}



void RenderEngine::SetDpi(float dpi) {



	m_dpi = dpi; // 【新增】存下 DPI



	if (m_d2dContext) {



		// 这行代码如同魔法，Direct2D 会自动将所有绘制（字体、圆角、图片）无损放大！



		m_d2dContext->SetDpi(dpi, dpi);



	}



}



void RenderEngine::Resize(int width, int height) {



	if (!m_dcompDevice || !m_d2dContext) return;







	// 释放旧的物理表面



	m_d2dContext->SetTarget(nullptr);



	m_targetBitmap.Reset();



	m_surface.Reset();







	// 根据新的 DPI 物理像素大小，重新创建 DComp 表面



	m_dcompDevice->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &m_surface);



	m_rootVisual->SetContent(m_surface.Get());



	m_dcompDevice->Commit();



}



void RenderEngine::DrawCapsule(const RenderContext& ctx)



{



	// 从 RenderContext 解构参数



	float islandWidth = ctx.islandWidth;



	float islandHeight = ctx.islandHeight;



	float canvasWidth = ctx.canvasWidth;



	float contentAlpha = ctx.contentAlpha;



	float audioLevel = ctx.audioLevel;



	const std::wstring& title = ctx.title;



	const std::wstring& artist = ctx.artist;



	bool isPlaying = ctx.isPlaying;



	bool hasSession = ctx.hasSession;



	bool showTime = ctx.showTime;



	const std::wstring& timeText = ctx.timeText;



	float progress = ctx.progress;



	int hoveredProgress = ctx.hoveredProgress;



	int pressedProgress = ctx.pressedProgress;



	const std::wstring& lyric = ctx.lyric.text;



	bool isVolumeActive = ctx.isVolumeActive;



	float volumeLevel = ctx.volumeLevel;



	bool isDragHovering = ctx.isDragHovering;



	size_t storedFileCount = ctx.storedFileCount;



	const std::wstring& weatherDesc = ctx.weatherDesc;



	float weatherTemp = ctx.weatherTemp;







	if (!m_surface) return;







	ComPtr<IDXGISurface> dxgiSurface;



	POINT offset;



	m_surface->BeginDraw(nullptr, __uuidof(IDXGISurface), (void**)&dxgiSurface, &offset);







	D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(



		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,



		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),



		m_dpi, m_dpi  // <--- 就是这里！补上这两个参数！



	);



	ComPtr<ID2D1Bitmap1> d2dTargetBitmap;







	m_d2dContext->CreateBitmapFromDxgiSurface(



		dxgiSurface.Get(),



		&bitmapProperties,



		&d2dTargetBitmap



	);







	m_d2dContext->SetTarget(d2dTargetBitmap.Get());



	//m_d2dContext->SetTarget(m_targetBitmap.Get());



	m_d2dContext->BeginDraw();



	m_d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));



	// m_d2dContext->SetTransform(D2D1::Matrix3x2F::Translation((float)offset.x, (float)offset.y));



	float dpiX, dpiY;



	m_d2dContext->GetDpi(&dpiX, &dpiY);



	float dpiScale = dpiX / 96.0f;



	m_d2dContext->SetTransform(D2D1::Matrix3x2F::Translation((float)offset.x / dpiScale, (float)offset.y / dpiScale));



	// 计算位置



	float left = (canvasWidth - islandWidth) / 2.0f;



	float top = 10.0f;



	float right = left + islandWidth;



	float bottom = top + islandHeight;







	// 【修改】圆角Radius平滑插值



	// 计算目标圆角值



	m_targetRadius = (islandHeight < 60.0f) ? (islandHeight / 2.0f) : 20.0f;



	// 平滑过渡（简单线性插值）



	m_currentRadius += (m_targetRadius - m_currentRadius) * 0.4f;



	float radius = m_currentRadius;







	// 1. 绘制底板
	D2D1_ROUNDED_RECT capsuleRect = D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), radius, radius);
	m_blackBrush->SetOpacity(1.0f); // 【修复】确保主岛背景始终不透明
	m_d2dContext->FillRoundedRectangle(&capsuleRect, m_blackBrush.Get());







	// 2. 【新增】如果内容透明度大于0，则绘制内部 UI（模拟音乐播放器面板）



	if (contentAlpha > 0.01f)



	{



		// --- 核心技巧：创建裁剪层 (Layer) ---



		// 这样内容绝对不会溢出到黑色的胶囊圆角之外



		ComPtr<ID2D1RoundedRectangleGeometry> clipGeometry;



		m_d2dFactory->CreateRoundedRectangleGeometry(&capsuleRect, &clipGeometry);







		ComPtr<ID2D1Layer> layer;



		m_d2dContext->CreateLayer(&layer);







		m_d2dContext->PushLayer(D2D1::LayerParameters(



			D2D1::InfiniteRect(), clipGeometry.Get(),



			D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1.0f, nullptr,



			D2D1_LAYER_OPTIONS_NONE



		), layer.Get());







		m_whiteBrush->SetOpacity(contentAlpha);

		m_grayBrush->SetOpacity(contentAlpha);

		m_themeBrush->SetOpacity(contentAlpha);


		const float COMPACT_THRESHOLD = 55.0f; // 紧凑模式高度阈值（降低以减少动画时的闪烁）


		// Mini态(<35)不进入紧凑模式渲染，Compact态(35-54)进入

		bool compactMode = (islandHeight >= 35.0f && islandHeight < COMPACT_THRESHOLD);

		if (m_isAlertActive)



		{



			// 判断是否是应用消息 (Type 3)



			bool isAppNotif = (m_currentAlert.type == 3);







			// 【优化】使用预创建的画刷，避免每帧创建



			ComPtr<ID2D1SolidColorBrush> iconBrush;



			if (m_currentAlert.type == 1) iconBrush = m_wifiBrush;



			else if (m_currentAlert.type == 2) iconBrush = m_bluetoothBrush;



			else if (m_currentAlert.type == 4) iconBrush = m_chargingBrush;



			else if (m_currentAlert.type == 5) iconBrush = m_lowBatteryBrush;



			else if (m_currentAlert.type == 6) iconBrush = m_fileBrush;



			else iconBrush = m_notificationBrush;



			



			// 设置透明度



			if (iconBrush) iconBrush->SetOpacity(contentAlpha);







			const wchar_t* iconText = L"";



			if (m_currentAlert.type == 1) iconText = L"\uE701";      // WiFi



			else if (m_currentAlert.type == 2) iconText = L"\uE702"; // 蓝牙



			else if (m_currentAlert.type == 4) iconText = L"\uEBB5"; // 【新增】充电插头/闪电 (Segoe MDL2)



			else if (m_currentAlert.type == 5) iconText = L"\uEBAE"; // 【新增】空电池警报



			else if (m_currentAlert.type == 6) iconText = L"\uE8A5"; // 【新增】文档图标







			if (compactMode)



			{



				// 【紧凑模式】：只显示图标 + "XXX 已连接" / "XXX 有新消息"



				float iconSize = 20.0f;



				std::wstring text = L"";



				// 【修改】按类型分配提示文字



				if (isAppNotif) text = m_currentAlert.name + L" 有新消息";



				else if (m_currentAlert.type == 1) text = L"Wi-Fi 已连接";



				else if (m_currentAlert.type == 2) text = m_currentAlert.deviceType + L" 已连接";



				else if (m_currentAlert.type == 4 || m_currentAlert.type == 5) text = m_currentAlert.name + L" (" + m_currentAlert.deviceType + L")";















				// 使用缓存的 TextLayout



				auto textLayout = GetOrCreateTextLayout(text, m_textFormatTitle.Get(), 10000.0f, L"alert_text");



				DWRITE_TEXT_METRICS metrics;



				textLayout->GetMetrics(&metrics);







				float totalWidth = iconSize + 8.0f + metrics.width;



				float startX = left + (islandWidth - totalWidth) / 2.0f; // 整体居中



				float textY = top + (islandHeight - metrics.height) / 2.0f;







				// 绘制图标 (是消息就画真实的App图片，是连接就画自带字符图标)



				D2D1_RECT_F iconRect = D2D1::RectF(startX, top + (islandHeight - iconSize) / 2.0f, startX + iconSize, top + (islandHeight - iconSize) / 2.0f + iconSize);



				if (isAppNotif && m_alertBitmap) {



					m_d2dContext->DrawBitmap(m_alertBitmap.Get(), iconRect, contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);



				}



				else if (isAppNotif) {







					m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(iconRect, 4.0f, 4.0f), m_themeBrush.Get());



				}



				else if (!isAppNotif) {



					m_d2dContext->DrawTextW(iconText, 1, m_iconTextFormat.Get(), D2D1::RectF(startX, top, startX + iconSize, bottom), iconBrush.Get());



				}







				m_d2dContext->DrawTextLayout(D2D1::Point2F(startX + iconSize + 8.0f, textY), textLayout.Get(), m_whiteBrush.Get());



			}



			else



			{



				// 【展开模式】：大连接卡片 / 大消息详情卡片



				float artSize = 60.0f;



				float artLeft = left + 20.0f;



				float artTop = top + 30.0f;



				D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(artLeft, artTop, artLeft + artSize, artTop + artSize), 12.0f, 12.0f);















				if (isAppNotif && m_alertBitmap) {



					m_d2dContext->DrawBitmap(m_alertBitmap.Get(), artRect.rect, contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);



				}



				else {



					m_d2dContext->FillRoundedRectangle(&artRect, iconBrush.Get());



					if (!isAppNotif) m_d2dContext->DrawTextW(iconText, 1, m_iconTextFormat.Get(), artRect.rect, m_whiteBrush.Get());



				}







				// 右侧画文字



				float textLeft = artLeft + artSize + 15.0f;



				float textRight = right - 20.0f;



				std::wstring topText = L"";



				std::wstring bottomText = L"";



				if (isAppNotif) {



					topText = m_currentAlert.name;



					bottomText = m_currentAlert.deviceType;



				}



				else if (m_currentAlert.type == 1) {



					topText = L"Wi-Fi";



					bottomText = m_currentAlert.name;



				}



				else if (m_currentAlert.type == 2) {



					topText = m_currentAlert.deviceType;



					bottomText = m_currentAlert.name;



				}



				else if (m_currentAlert.type == 4 || m_currentAlert.type == 5) {



					// 【新增】电量提示逻辑



					topText = m_currentAlert.deviceType; // 如 "85%" 或 "请连接电源"



					bottomText = m_currentAlert.name;    // 如 "电源已连接" 或 "电量不足 20%"



				}







				// 上方小灰字 (例如 "微信" 或 "Wi-Fi")



				auto topLayout = GetOrCreateTextLayout(topText, m_textFormatSub.Get(), 10000.0f, L"alert_top");



				m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 5.0f), topLayout.Get(), m_grayBrush.Get());







				// 下方大粗字 (例如 "张三: 在吗？" 或 "AirPods Pro")



				if (!bottomText.empty()) {



					auto botLayout = GetOrCreateTextLayout(bottomText, m_textFormatTitle.Get(), 10000.0f, L"alert_bottom");



					botLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);







					m_d2dContext->PushAxisAlignedClip(D2D1::RectF(textLeft, artTop + 25.0f, textRight, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);



					m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeft, artTop + 28.0f), botLayout.Get(), m_whiteBrush.Get());



					m_d2dContext->PopAxisAlignedClip();



				}



			}



		}











		// ============================================



		 // 【新增】：如果正在调音量，直接画音量条！



		 // ============================================



		else if (isVolumeActive && compactMode && !m_isAlertActive)



		{



			float iconSize = 20.0f;



			float barWidth = 120.0f;



			float barHeight = 6.0f;



			float totalWidth = iconSize + 15.0f + barWidth;



			float startX = left + (islandWidth - totalWidth) / 2.0f;











			// 1. 根据音量大小选择不同的扬声器图标 (Segoe MDL2 Assets)



			const wchar_t* volIcon = L"\uE995"; // 3格音量 (大) (> 65%)



			if (volumeLevel <= 0.0f) volIcon = L"\uE74F"; // 静音 (0%)



			else if (volumeLevel <= 0.35f) volIcon = L"\uE992"; // 1格音量 (小) (1% - 35%)



			else if (volumeLevel <= 0.65f) volIcon = L"\uE993"; // 2格音量 (中) (36% - 65%)







			m_d2dContext->DrawTextW(volIcon, 1, m_iconTextFormat.Get(),



				D2D1::RectF(startX, top, startX + iconSize, bottom), m_whiteBrush.Get());







			// 2. 绘制音量条底槽 (半透明灰色) - 使用预创建的画刷



			float barY = top + (islandHeight - barHeight) / 2.0f;



			float barX = startX + iconSize + 15.0f;



			m_grayBrush->SetOpacity(0.5f * contentAlpha);



			m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight), barHeight / 2.0f, barHeight / 2.0f), m_grayBrush.Get());







			// 3. 绘制白色进度前景



			if (volumeLevel > 0.0f) {



				m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barWidth * volumeLevel, barY + barHeight), barHeight / 2.0f, barHeight / 2.0f), m_whiteBrush.Get());



			}



		}



		// ============================================



		// 如果正在报警弹窗，完全屏蔽音乐 UI



		// ============================================







		else



		{



			if (isDragHovering) {



				float iconSize = 28.0f;



				float startX = left + (islandWidth - iconSize) / 2.0f;



				float startY = top + (islandHeight - iconSize) / 2.0f - 12.0f;







				// 画一个向下的箭头图标



				m_d2dContext->DrawTextW(L"\uE8E5", 1, m_iconTextFormat.Get(),



					D2D1::RectF(startX, startY, startX + iconSize, startY + iconSize), m_themeBrush.Get());







				// 画文字 - 使用缓存的 TextLayout



				std::wstring text = L"松开以暂存文件";



				auto textLayout = GetOrCreateTextLayout(text, m_textFormatSub.Get(), 200.0f, L"drag_hint");



				DWRITE_TEXT_METRICS metrics;



				textLayout->GetMetrics(&metrics);



				m_d2dContext->DrawTextLayout(D2D1::Point2F(left + (islandWidth - metrics.width) / 2.0f, startY + 35.0f), textLayout.Get(), m_whiteBrush.Get());







			}



			// 显示时间：如果没有通知、没有播放、没有寄存文件需要显示时间（暂停也显示时间）



			else if (storedFileCount > 0) {







				if (compactMode) {



					// 紧凑模式：显示一个文件夹图标 + 数量



					float iconSize = 18.0f;



					std::wstring text = L"已暂存 " + std::to_wstring(storedFileCount) + L" 个文件";







					// 使用缓存的 TextLayout（key包含文件数量）



					auto textLayout = GetOrCreateTextLayout(text, m_textFormatTitle.Get(), 200.0f, L"stored_count_" + std::to_wstring(storedFileCount));



					DWRITE_TEXT_METRICS metrics;



					textLayout->GetMetrics(&metrics);







					float totalWidth = iconSize + 8.0f + metrics.width;



					float startX = left + (islandWidth - totalWidth) / 2.0f;



					float textY = top + (islandHeight - metrics.height) / 2.0f;







					// 绘制文件夹图标 \uE8B7



					m_d2dContext->DrawTextW(L"\uE8B7", 1, m_iconTextFormat.Get(),



						D2D1::RectF(startX, top + (islandHeight - iconSize) / 2.0f, startX + iconSize, top + (islandHeight - iconSize) / 2.0f + iconSize), m_themeBrush.Get());



					m_d2dContext->DrawTextLayout(D2D1::Point2F(startX + iconSize + 8.0f, textY), textLayout.Get(), m_whiteBrush.Get());



				}



				else



				{



					// 展开模式：大图标展示



					float artSize = 60.0f;



					float artLeft = left + 20.0f;



					float artTop = top + 30.0f;



					D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(artLeft, artTop, artLeft + artSize, artTop + artSize), 12.0f, 12.0f);







					// 深灰色底板 - 使用预创建的画刷



					m_darkGrayBrush->SetOpacity(contentAlpha);



					m_d2dContext->FillRoundedRectangle(&artRect, m_darkGrayBrush.Get());







					// 文件夹大图标 \uE8B7



					m_d2dContext->DrawTextW(L"\uE8B7", 1, m_iconTextFormat.Get(),



						D2D1::RectF(artLeft, artTop, artLeft + artSize, artTop + artSize), m_themeBrush.Get());







					// 绘制文字



					float textLeftFile = artLeft + artSize + 15.0f;



					std::wstring textTop = L"文件暂存区";



					std::wstring textBot = std::to_wstring(storedFileCount) + L" 个文件 (点击全部打开)";







					auto topLayout = GetOrCreateTextLayout(textTop, m_textFormatSub.Get(), 200.0f, L"file_top");



					auto botLayout = GetOrCreateTextLayout(textBot, m_textFormatTitle.Get(), 200.0f, L"file_bottom");







					m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeftFile, artTop + 5.0f), topLayout.Get(), m_grayBrush.Get());



					m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeftFile, artTop + 28.0f), botLayout.Get(), m_whiteBrush.Get());



				}



			}







			else if (compactMode) {



				// Compact 态 (35-59) 显示时间，Mini 态 (<35) 不显示时间



				// 中间：显示时间



				if (showTime && !timeText.empty()) {



					auto timeLayout = GetOrCreateTextLayout(timeText, m_textFormatTitle.Get(), 200.0f, L"time_" + timeText);



					DWRITE_TEXT_METRICS metrics;



					timeLayout->GetMetrics(&metrics);







					float textX = left + (islandWidth - metrics.width) / 2.0f;



					float textY = top + (islandHeight - metrics.height) / 2.0f;







					m_d2dContext->DrawTextLayout(



						D2D1::Point2F(textX, textY),



						timeLayout.Get(),



						m_whiteBrush.Get()



					);



				}







				// 右边：显示天气图标 + 温度（仅在不播放音乐时）



				if (!isPlaying && !weatherDesc.empty() && weatherTemp != 0.0f) {



					// [X] Update animation phase



					ULONGLONG currentTime = GetTickCount64();



					if (m_lastWeatherAnimTime == 0) m_lastWeatherAnimTime = currentTime;



					float dt = (float)(currentTime - m_lastWeatherAnimTime) / 1000.0f;



					m_weatherAnimPhase += dt * 2.0f;



					m_lastWeatherAnimTime = currentTime;







					// [X] Update weather type on description change



					WeatherType newType = MapWeatherDescToType(weatherDesc);



					if (m_weatherType != newType) {



						m_weatherType = newType;



						m_weatherAnimPhase = 0.0f;



					}







					// [X] Draw geometric weather icon



					float iconSize = islandHeight * 0.4f;



					float iconX = right - iconSize - 15.0f;



					float iconY = top + (islandHeight - iconSize) / 2.0f;



					DrawWeatherIcon(iconX, iconY, iconSize, contentAlpha, currentTime);







					// Temperature



					std::wstring tempText = std::to_wstring((int)weatherTemp) + L"\u00B0";



					auto tempLayout = GetOrCreateTextLayout(tempText, m_textFormatSub.Get(), 80.0f, L"weather_temp");



					DWRITE_TEXT_METRICS tempMetrics;



					tempLayout->GetMetrics(&tempMetrics);



					float tempX = iconX - tempMetrics.width - 5.0f;



					float tempY = top + (islandHeight - tempMetrics.height) / 2.0f;



					m_d2dContext->DrawTextLayout(D2D1::Point2F(tempX, tempY), tempLayout.Get(), m_whiteBrush.Get());



				}



				if (isPlaying) {



					// 波形绘制（保持不变）







					float waveRight = right - 20.0f;



					float baseBottom = top + islandHeight - 10.0f;



					float h1 = m_currentWaveHeight[0] * 0.5f;



					float h2 = m_currentWaveHeight[1] * 0.5f;



					float h3 = m_currentWaveHeight[2] * 0.5f;



					m_d2dContext->FillRectangle(D2D1::RectF(waveRight - 20.0f, baseBottom - h1, waveRight - 16.0f, baseBottom), m_themeBrush.Get());



					m_d2dContext->FillRectangle(D2D1::RectF(waveRight - 12.0f, baseBottom - h2, waveRight - 8.0f, baseBottom), m_themeBrush.Get());



					m_d2dContext->FillRectangle(D2D1::RectF(waveRight - 4.0f, baseBottom - h3, waveRight, baseBottom), m_themeBrush.Get());







					// 构建完整显示文本：歌名 + - + 艺术家



					std::wstring fullText = title;



					fullText += L" ";











					// 文本区域



					float textLeft = left + 15.0f;



					float textRight = waveRight - 30.0f;



					if (textRight > textLeft) {



						float maxWidth = textRight - textLeft;



						// 使用缓存的 TextLayout（key包含歌曲信息）



						auto textLayout = GetOrCreateTextLayout(fullText, m_textFormatTitle.Get(), maxWidth, L"playing_" + fullText);



						if (textLayout) {



							textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);



							DWRITE_TEXT_METRICS metrics;



							textLayout->GetMetrics(&metrics);



							float textWidth = metrics.width;



							float textHeight = metrics.height;   // 实际文本高度



							bool needScroll = (textWidth > maxWidth);







							// 文本变化时重置偏移



							if (fullText != m_lastDrawnFullText) {



								m_titleScrollOffset = 0.0f;



								m_lastDrawnFullText = fullText;



							}



							m_titleScrolling = needScroll;







							float yPos = top + (islandHeight - textHeight) / 2.0f;



							m_whiteBrush->SetOpacity(contentAlpha);







							// 裁剪区域



							m_d2dContext->PushAxisAlignedClip(



								D2D1::RectF(textLeft, top, textRight, bottom),



								D2D1_ANTIALIAS_MODE_PER_PRIMITIVE



							);







							if (needScroll) {



								// 循环滚动：偏移量取模文本宽度，绘制两个副本



								float offset = fmod(m_titleScrollOffset, textWidth);



								// 第一个副本



								m_d2dContext->DrawTextLayout(



									D2D1::Point2F(textLeft - offset, yPos),



									textLayout.Get(),



									m_whiteBrush.Get()



								);



								// 第二个副本（紧跟在右侧）



								m_d2dContext->DrawTextLayout(



									D2D1::Point2F(textLeft - offset + textWidth, yPos),



									textLayout.Get(),



									m_whiteBrush.Get()



								);



							}



							else {



								// 文本不超出时居中显示



								float textOffset = (maxWidth - textWidth) / 2.0f;



								m_d2dContext->DrawTextLayout(



									D2D1::Point2F(textLeft + textOffset, yPos),



									textLayout.Get(),



									m_whiteBrush.Get()



								);



							}







							m_d2dContext->PopAxisAlignedClip();



						}



					}



				}



			}











			else // 展开模式



			{



				// 固定参数



				float artSize = 60.0f;



				float artLeft = left + 20.0f;



				float artTop = top + 30.0f;



				D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(artLeft, artTop, artLeft + artSize, artTop + artSize), 12.0f, 12.0f);







				// 文本区域



				float textLeft = artLeft + artSize + 15.0f;



				float textRight = right - 20.0f;



				float titleMaxWidth = textRight - textLeft;







				if (hasSession && isPlaying)



				{



					// ---------- 有媒体会话且正在播放：显示音乐播放界面 ----------



					// 绘制专辑封面



					if (m_albumBitmap) {



						m_d2dContext->DrawBitmap(m_albumBitmap.Get(), artRect.rect, contentAlpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);



					}



					else {



						m_themeBrush->SetOpacity(contentAlpha);



						m_d2dContext->FillRoundedRectangle(&artRect, m_themeBrush.Get());



					}







					// 显示歌词（在歌名和歌手上面，左对齐）



					// 在 DrawCapsule 中，找到展开模式下的歌词绘制部分（约在 if (!lyric.empty()) 处）



					if (!lyric.empty()) {



						float lyricLeft = artLeft + artSize + 15.0f;



						float lyricWidth = (right - 20.0f) - lyricLeft;



						D2D1_RECT_F lyricRect = D2D1::RectF(lyricLeft, artTop, lyricLeft + lyricWidth, artTop + 25.0f);







						// 使用缓存的 TextLayout（key包含歌词内容）



						auto lyricLayout = GetOrCreateTextLayout(lyric, m_textFormatTitle.Get(), lyricWidth, L"lyric_" + lyric);



						if (lyricLayout)



						{



							lyricLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);



							lyricLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);







							DWRITE_TEXT_METRICS metrics;



							lyricLayout->GetMetrics(&metrics);







							// 判断是否需要滚动



							bool needScroll = (metrics.width > lyricWidth);



							if (needScroll != m_lyricScrolling || lyric != m_lastDrawnLyric) {



								m_lyricScrolling = needScroll;



								if (lyric != m_lastDrawnLyric) {



									m_lyricScrollOffset = 0.0f;



									m_lastDrawnLyric = lyric;



								}



							}







							// 裁剪区域



							m_d2dContext->PushAxisAlignedClip(lyricRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);







							if (needScroll) {



								// 循环滚动



								float offset = fmod(m_lyricScrollOffset, metrics.width);



								float yPos = lyricRect.top;



								m_d2dContext->DrawTextLayout(



									D2D1::Point2F(lyricLeft - offset, yPos),



									lyricLayout.Get(),



									m_whiteBrush.Get());



								m_d2dContext->DrawTextLayout(



									D2D1::Point2F(lyricLeft - offset + metrics.width, yPos),



									lyricLayout.Get(),



									m_whiteBrush.Get());



							}



							else {



								// 左对齐显示（如果不需要滚动）



								m_d2dContext->DrawTextLayout(



									D2D1::Point2F(lyricLeft, lyricRect.top),



									lyricLayout.Get(),



									m_whiteBrush.Get());



							}







							m_d2dContext->PopAxisAlignedClip();



							



							// 【临时禁用】绘制歌词遮罩渐变（左右边缘渐变提示）



							//float maskWidth = 30.0f;



							//// 左侧遮罩



							//m_d2dContext->FillRectangle(



							//	D2D1::RectF(lyricLeft, lyricRect.top, lyricLeft + maskWidth, lyricRect.bottom),



							//	m_lyricFadeLeftBrush.Get()



							//);



							//// 右侧遮罩



							//m_d2dContext->FillRectangle(



							//	D2D1::RectF(lyricLeft + lyricWidth - maskWidth, lyricRect.top, lyricLeft + lyricWidth, lyricRect.bottom),



							//	m_lyricFadeRightBrush.Get()



							//);



						}



					}







					// 绘制"歌名 - 歌手"（在歌词下方，左对齐）- 使用省略号，不换行



					std::wstring combinedText = title;



					if (!artist.empty()) {



						combinedText += L" - " + artist;



					}







					float combinedWidth = titleMaxWidth - 60.0f;



					D2D1_RECT_F combinedRect = D2D1::RectF(textLeft, artTop + 25.0f, textLeft + combinedWidth, artTop + 50.0f);







					// 使用缓存的 TextLayout



					auto combinedLayout = GetOrCreateTextLayout(combinedText, m_textFormatSub.Get(), combinedWidth, L"combined_" + combinedText);



					if (combinedLayout)



					{



						combinedLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);







						// 设置省略号（字符级）



						DWRITE_TRIMMING trimming{};



						trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;







						ComPtr<IDWriteInlineObject> ellipsis;



						m_dwriteFactory->CreateEllipsisTrimmingSign(combinedLayout.Get(), &ellipsis);







						combinedLayout->SetTrimming(&trimming, ellipsis.Get());







						DWRITE_TEXT_METRICS metrics;



						combinedLayout->GetMetrics(&metrics);







						m_d2dContext->DrawTextLayout(D2D1::Point2F(textLeft, combinedRect.top),



							combinedLayout.Get(), m_grayBrush.Get());



					}







					// 绘制波形（仅在播放时）



					if (isPlaying) {



						float waveRight = right - 40.0f;



						float baseBottom = artTop + 45.0f;



						float h1 = m_currentWaveHeight[0] * 0.5f;



						float h2 = m_currentWaveHeight[1] * 0.5f;



						float h3 = m_currentWaveHeight[2] * 0.5f;



						m_d2dContext->FillRectangle(D2D1::RectF(waveRight - 20.0f, baseBottom - h1, waveRight - 16.0f, baseBottom), m_themeBrush.Get());



						m_d2dContext->FillRectangle(D2D1::RectF(waveRight - 12.0f, baseBottom - h2, waveRight - 8.0f, baseBottom), m_themeBrush.Get());



						m_d2dContext->FillRectangle(D2D1::RectF(waveRight - 4.0f, baseBottom - h3, waveRight, baseBottom), m_themeBrush.Get());



					}







					// 绘制进度条（在专辑封面正下方，和文本区域同宽）



					float artistBottom = artTop + 60.0f;



					float progressBarY = artistBottom + 20.0f;



					float progressBarHeight = 6.0f;







					// 悬停时放大进度条 (动画效果)



					if (hoveredProgress != -1 || pressedProgress != -1) {



						progressBarHeight = 10.0f;



						progressBarY -= 2.0f;



					}







					float progressBarLeft = textLeft - 80.0f;



					float progressBarRight = textLeft + titleMaxWidth;







					// 进度条背景 - 使用预创建的画刷



					m_progressBgBrush->SetOpacity(0.5f * contentAlpha);







					D2D1_ROUNDED_RECT progressBgRect = D2D1::RoundedRect(



						D2D1::RectF(progressBarLeft, progressBarY, progressBarRight, progressBarY + progressBarHeight),



						progressBarHeight / 2.0f, progressBarHeight / 2.0f



					);



					m_d2dContext->FillRoundedRectangle(&progressBgRect, m_progressBgBrush.Get());







					// 进度条前景 - 使用预创建的画刷



					m_progressFgBrush->SetOpacity(contentAlpha);







					float progressWidth = (progressBarRight - progressBarLeft) * progress;



					D2D1_ROUNDED_RECT progressFgRect = D2D1::RoundedRect(



						D2D1::RectF(progressBarLeft, progressBarY, progressBarLeft + progressWidth, progressBarY + progressBarHeight),



						progressBarHeight / 2.0f, progressBarHeight / 2.0f



					);



					m_d2dContext->FillRoundedRectangle(&progressFgRect, m_progressFgBrush.Get());







					// 绘制播放控制按钮（在进度条下面）



					float buttonY = progressBarY + progressBarHeight + 10.0f;



					float buttonGroupWidth = Constants::UI::BUTTON_SIZE * 3 + Constants::UI::BUTTON_SPACING * 2;



					float buttonX = textLeft + (titleMaxWidth - buttonGroupWidth) / 2.0f - 45.0f;



					if (buttonX < textLeft) buttonX = textLeft;



					DrawPlaybackButtons(buttonX, buttonY, Constants::UI::BUTTON_SIZE, contentAlpha, isPlaying);



				}







				else



				{



					// ---------- 无媒体会话：不显示任何内容 ----------



					// 空白，什么都不画



				}



			}



		}

		m_d2dContext->PopLayer();  // 对应 PushLayer



	}



	// --- [新增] 在展开模式下，渲染一个独立的“副岛” (Multi-Island) ---
	// 使用动画高度驱动，不再仅依赖 isVolumeActive 布尔值，以允许弹簧缩回动画
	float secHeight = ctx.secondaryHeight;
	float secAlpha = ctx.secondaryAlpha;

	if (secHeight > 0.1f)
	{
		float secWidth = Constants::Size::SECONDARY_WIDTH;
		float secLeft = left + (islandWidth - secWidth) / 2.0f;
		float secTop = bottom + 12.0f; 
		float secRight = secLeft + secWidth;
		float secBottom = secTop + secHeight;
		float secRadius = secHeight / 2.0f;

		// 1. 绘制副岛底色
		D2D1_ROUNDED_RECT secRect = D2D1::RoundedRect(D2D1::RectF(secLeft, secTop, secRight, secBottom), secRadius, secRadius);
		m_blackBrush->SetOpacity(secAlpha);
		m_d2dContext->FillRoundedRectangle(&secRect, m_blackBrush.Get());

		// 2. 绘制副岛内部内容 (仅在高度足够时显示内容，防止挤压感)
		if (secHeight > 15.0f) {
			float contentAlpha = secAlpha * ((secHeight - 15.0f) / (Constants::Size::SECONDARY_HEIGHT - 15.0f));
			float iconSize = 18.0f;
			float barWidth = secWidth - iconSize - 40.0f;
			float barHeight = 5.0f;
			float startX = secLeft + (secWidth - (iconSize + 15.0f + barWidth)) / 2.0f;
			float barY = secTop + (secHeight - barHeight) / 2.0f;

			// 选择图标
			const wchar_t* volIcon = L"\uE995";
			if (volumeLevel <= 0.0f) volIcon = L"\uE74F";
			else if (volumeLevel <= 0.35f) volIcon = L"\uE992";
			else if (volumeLevel <= 0.65f) volIcon = L"\uE993";

			// 绘制图标
			m_whiteBrush->SetOpacity(contentAlpha);
			m_d2dContext->DrawTextW(volIcon, 1, m_iconTextFormat.Get(),
				D2D1::RectF(startX, secTop, startX + iconSize, secBottom), m_whiteBrush.Get());

			// 绘制进度槽
			float barX = startX + iconSize + 15.0f;
			m_grayBrush->SetOpacity(0.4f * contentAlpha);
			m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight), barHeight / 2.0f, barHeight / 2.0f), m_grayBrush.Get());

			// 绘制进度条
			if (volumeLevel > 0.0f) {
				m_whiteBrush->SetOpacity(contentAlpha);
				m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barWidth * volumeLevel, barY + barHeight), barHeight / 2.0f, barHeight / 2.0f), m_whiteBrush.Get());
			}
		}
	}



	m_d2dContext->EndDraw();



	m_surface->EndDraw();



	m_dcompDevice->Commit();



}



bool RenderEngine::LoadAlbumArt(const std::wstring& file)



{



	if (!m_wicFactory)



		return false;







	ComPtr<IWICBitmapDecoder> decoder;







	HRESULT hr = m_wicFactory->CreateDecoderFromFilename(



		file.c_str(),



		nullptr,



		GENERIC_READ,



		WICDecodeMetadataCacheOnLoad,



		&decoder



	);







	if (FAILED(hr))



		return false;







	ComPtr<IWICBitmapFrameDecode> frame;



	decoder->GetFrame(0, &frame);







	ComPtr<IWICFormatConverter> converter;







	m_wicFactory->CreateFormatConverter(&converter);







	converter->Initialize(



		frame.Get(),



		GUID_WICPixelFormat32bppPBGRA,



		WICBitmapDitherTypeNone,



		nullptr,



		0,



		WICBitmapPaletteTypeCustom



	);







	m_d2dContext->CreateBitmapFromWicBitmap(



		converter.Get(),



		nullptr,



		&m_albumBitmap



	);







	return true;



}



void RenderEngine::UpdateScroll(float deltaTime, float audioLevel, float islandHeight, const struct LyricData& lyric)



{



	// 检测是否处于模式切换过渡中



	const float COMPACT_THRESHOLD = 60.0f;



	const float EXPANDED_HEIGHT = 120.0f;



	const float TRANSITION_ZONE = 30.0f;  // 过渡区域大小







	// 判断当前状态和过渡方向



	bool wasCompact = (m_lastIslandHeight > 0 && m_lastIslandHeight < COMPACT_THRESHOLD);



	bool isCompact = (islandHeight >= 35.0f && islandHeight < COMPACT_THRESHOLD);



	bool isExpanded = (islandHeight >= COMPACT_THRESHOLD);







	// 如果从compact跨到expanded或反之，重置滚动



	if (m_lastIslandHeight > 0) {



		if ((wasCompact && isExpanded) || (!wasCompact && !isExpanded && islandHeight < m_lastIslandHeight)) {



			// 模式切换，重置滚动



			m_titleScrollOffset = 0.0f;



			m_artistScrollOffset = 0.0f;



			m_lyricScrollOffset = 0.0f;



		}



	}



	m_lastIslandHeight = islandHeight;







	// 标题滚动



	if (m_titleScrolling)



	{



		m_titleScrollOffset += Constants::Animation::SCROLL_SPEED * deltaTime;



		// 假设我们已知文本宽度和可用宽度，但这里无法获取，需要在绘制时更新



		// 因此我们改为在每次绘制时根据当前文本重新计算是否需要滚动和最大偏移



		// 所以这里我们只做增量，但需要确保不超过最大偏移



		// 我们将在绘制时计算最大偏移，并 clamp



	}



	else



	{



		m_titleScrollOffset = 0.0f;



	}







	// 艺术家滚动



	if (m_artistScrolling)



	{



		m_artistScrollOffset += Constants::Animation::SCROLL_SPEED * deltaTime;



	}



	else



	{



		m_artistScrollOffset = 0.0f;



	}



	// 新增：歌词滚动更新



	if (m_lyricScrolling) {
		float scrollSpeed = Constants::Animation::SCROLL_SPEED;
		// Smart deceleration: reduce speed in last 2 seconds of lyric
		if (lyric.nextMs > 0 && lyric.positionMs > 0) {
			float remaining = (float)(lyric.nextMs - lyric.positionMs) / 1000.0f;
			if (remaining > 0.0f && remaining < 2.0f) {
				float t = remaining / 2.0f;
				float ease = t * t * (3.0f - 2.0f * t);  // smoothstep
				scrollSpeed *= ease;
			}
		}
		// Spring physics for smooth scrolling
		const float stiffness = 8.0f;
		const float damping = 0.5f;
		m_lyricScrollVelocity += (scrollSpeed - m_lyricScrollVelocity) * stiffness * 0.016f;
		m_lyricScrollVelocity *= (1.0f - damping * 0.016f);
		m_lyricScrollOffset += m_lyricScrollVelocity * deltaTime;
	}
	else {
		m_lyricScrollOffset = 0.0f;
		m_lyricScrollVelocity = 0.0f;
	}



	// 更新目标波形高度（原有随机逻辑）



	const float minH = 5.0f;



	const float maxAdd = audioLevel * 50.0f;  // 最大额外高度



	for (int i = 0; i < 3; ++i) {



		float target = minH + (rand() / float(RAND_MAX)) * maxAdd;



		// 新增：根据岛屿高度限制目标高度（保留上下边距）



		float maxAllowed = islandHeight - 12.0f; // 留出6像素上下边距



		if (target > maxAllowed) target = maxAllowed;



		m_targetWaveHeight[i] = target;



	}







	// 平滑过渡



	const float smoothFactor = 0.5f;



	for (int i = 0; i < 3; ++i) {



		m_currentWaveHeight[i] += (m_targetWaveHeight[i] - m_currentWaveHeight[i]) * smoothFactor;



	}



}







// 绘制播放控制按钮（上一曲、播放/暂停、下一曲）



void RenderEngine::DrawPlaybackButtons(float left, float top, float buttonSize, float contentAlpha, bool isPlaying) {



	float spacing = Constants::UI::BUTTON_SPACING;



	float y = top;







	const wchar_t prevIcon = L'\uE892';



	const wchar_t playIcon = L'\uE768';



	const wchar_t pauseIcon = L'\uE769';



	const wchar_t nextIcon = L'\uE893';







	m_whiteBrush->SetOpacity(contentAlpha);







	auto DrawButton = [&](int index, float x, float y, wchar_t icon) {



		D2D1_RECT_F rect = D2D1::RectF(x, y, x + buttonSize, y + buttonSize);



		bool isHovered = (index == m_hoveredButtonIndex);



		bool isPressed = (index == m_pressedButtonIndex);







		// 1. 绘制悬浮/按下的半透明背景 - 使用预创建的画刷



		if (contentAlpha > 0.1f && (isHovered || isPressed)) {



			float bgOpacity = isPressed ? 0.25f : 0.12f;



			m_buttonHoverBrush->SetOpacity(bgOpacity * contentAlpha);







			// 【修复】半径设为宽度的一半，变成完美的圆形！



			D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(rect, buttonSize / 2.0f, buttonSize / 2.0f);



			m_d2dContext->FillRoundedRectangle(&roundedRect, m_buttonHoverBrush.Get());



		}







		// 2. 绘制图标（按下时缩放）



		if (isPressed) {



			D2D1_MATRIX_3X2_F oldTransform;



			m_d2dContext->GetTransform(&oldTransform);







			float centerX = x + buttonSize / 2.0f;



			float centerY = y + buttonSize / 2.0f;



			D2D1_MATRIX_3X2_F scale = D2D1::Matrix3x2F::Scale(0.8f, 0.8f, D2D1::Point2F(centerX, centerY));



			m_d2dContext->SetTransform(scale * oldTransform);







			m_d2dContext->DrawText(&icon, 1, m_iconTextFormat.Get(), rect, m_whiteBrush.Get());



			m_d2dContext->SetTransform(oldTransform);



		}



		else {



			m_d2dContext->DrawText(&icon, 1, m_iconTextFormat.Get(), rect, m_whiteBrush.Get());



		}



		};







	DrawButton(0, left, y, prevIcon);



	DrawButton(1, left + buttonSize + spacing, y, isPlaying ? pauseIcon : playIcon);



	DrawButton(2, left + 2 * (buttonSize + spacing), y, nextIcon);



}



bool RenderEngine::LoadAlertIcon(const std::wstring& file) {







	m_alertBitmap.Reset();



	if (!m_wicFactory || file.empty()) return false;







	// 等待文件释放



	Sleep(100);







	ComPtr<IWICBitmapDecoder> decoder;



	HRESULT hr = E_FAIL;







	// 增加重试逻辑，最多重试 5 次



	for (int i = 0; i < 5; ++i) {



		hr = m_wicFactory->CreateDecoderFromFilename(



			file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);



		if (SUCCEEDED(hr)) break;



		Sleep(100); // 等待文件解锁



	}







	if (FAILED(hr) || !decoder) {



		OutputDebugStringW((L"RenderEngine: 无法加载图标文件: " + file + L"\n").c_str());



		return false;



	}







	ComPtr<IWICBitmapFrameDecode> frame;



	decoder->GetFrame(0, &frame);







	ComPtr<IWICFormatConverter> converter;



	m_wicFactory->CreateFormatConverter(&converter);



	converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,



		WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);







	hr = m_d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_alertBitmap);



	if (FAILED(hr)) {



		OutputDebugStringW(L"RenderEngine: D2D 无法创建Bitmap\n");



		return false;



	}



	return true;



}







bool RenderEngine::LoadAlbumArtFromMemory(const std::vector<uint8_t>& data) {



	m_albumBitmap.Reset();



	if (!m_wicFactory || data.empty()) return false;







	ComPtr<IWICStream> wicStream;



	HRESULT hr = m_wicFactory->CreateStream(&wicStream);



	if (FAILED(hr)) return false;







	hr = wicStream->InitializeFromMemory(const_cast<uint8_t*>(data.data()), static_cast<DWORD>(data.size()));



	if (FAILED(hr)) return false;







	ComPtr<IWICBitmapDecoder> decoder;



	hr = m_wicFactory->CreateDecoderFromStream(wicStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);



	if (FAILED(hr)) return false;







	ComPtr<IWICBitmapFrameDecode> frame;



	decoder->GetFrame(0, &frame);







	ComPtr<IWICFormatConverter> converter;



	m_wicFactory->CreateFormatConverter(&converter);



	converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,



		WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);







	hr = m_d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_albumBitmap);



	if (FAILED(hr)) {



		OutputDebugStringW(L"RenderEngine: D2D 无法从内存创建Bitmap\n");



		return false;



	}



	return true;



}







bool RenderEngine::LoadAlertIconFromMemory(const std::vector<uint8_t>& data) {



	m_alertBitmap.Reset();



	if (!m_wicFactory || data.empty()) return false;







	ComPtr<IWICStream> wicStream;



	HRESULT hr = m_wicFactory->CreateStream(&wicStream);



	if (FAILED(hr)) return false;







	hr = wicStream->InitializeFromMemory(const_cast<uint8_t*>(data.data()), static_cast<DWORD>(data.size()));



	if (FAILED(hr)) return false;







	ComPtr<IWICBitmapDecoder> decoder;



	hr = m_wicFactory->CreateDecoderFromStream(wicStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);



	if (FAILED(hr)) return false;







	ComPtr<IWICBitmapFrameDecode> frame;



	decoder->GetFrame(0, &frame);







	ComPtr<IWICFormatConverter> converter;



	m_wicFactory->CreateFormatConverter(&converter);



	converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,



		WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);







	hr = m_d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &m_alertBitmap);



	if (FAILED(hr)) {



		OutputDebugStringW(L"RenderEngine: D2D 无法从内存创建通知图标Bitmap\n");



		return false;



	}



	return true;



}







// 【新增】获取或创建 TextLayout（带缓存）



ComPtr<IDWriteTextLayout> RenderEngine::GetOrCreateTextLayout(



    const std::wstring& text,



    IDWriteTextFormat* format,



    float maxWidth,



    const std::wstring& cacheKey)



{



    // 检查缓存是否存在且文本未变化



    auto it = m_textLayoutCache.find(cacheKey);



    if (it != m_textLayoutCache.end()) {



        // 缓存命中且文本相同，直接返回



        if (it->second.text == text && it->second.maxWidth == maxWidth) {



            return it->second.layout;



        }



        // 文本不同，删除旧缓存



        m_textLayoutCache.erase(it);



    }







    // 创建新的 TextLayout



    ComPtr<IDWriteTextLayout> textLayout;



    HRESULT hr = m_dwriteFactory->CreateTextLayout(



        text.c_str(),



        (UINT32)text.length(),



        format,



        maxWidth,



        100.0f,  // 固定高度足够



        &textLayout



    );







    if (SUCCEEDED(hr)) {



        // 添加到缓存



        TextLayoutCacheEntry entry;



        entry.layout = textLayout;



        entry.text = text;



        entry.maxWidth = maxWidth;



        m_textLayoutCache[cacheKey] = entry;



    }







    return textLayout;



}















// =====================================================================



// Weather Icon Drawing - Direct2D Geometric Implementation



// =====================================================================







RenderEngine::WeatherType RenderEngine::MapWeatherDescToType(const std::wstring& desc) const {



    std::wstring d = desc;



    for (auto& c : d) c = towlower(c);



    if (d.find(L'雪') != (size_t)-1 || d.find(L'霜') != (size_t)-1)



        return WeatherType::Snow;



    if (d.find(L'雾') != (size_t)-1 || d.find(L'霾') != (size_t)-1)



        return WeatherType::Fog;



    if (d.find(L'雷暴') != (size_t)-1 || d.find(L'雷电') != (size_t)-1)



        return WeatherType::Thunder;



    if (d.find(L'雨') != (size_t)-1 || d.find(L'阵雨') != (size_t)-1)



        return WeatherType::Rainy;



    if (d.find(L'晴') != (size_t)-1 && d.find(L'多云') == (size_t)-1 && d.find(L'阴') == (size_t)-1)



        return WeatherType::Clear;



    if (d.find(L'多云') != (size_t)-1 || d.find(L'间晴') != (size_t)-1 || d.find(L'间多云') != (size_t)-1)



        return WeatherType::PartlyCloudy;



    if (d.find(L'阴') != (size_t)-1)



        return WeatherType::Cloudy;



    return WeatherType::Default;



}







void RenderEngine::DrawWeatherIcon(float x, float y, float size, float alpha, ULONGLONG currentTime) {



    m_whiteBrush->SetOpacity(alpha);



    float cx = x + size * 0.5f;



    float cy = y + size * 0.5f;



    float r = size * 0.45f;







    switch (m_weatherType) {



        case WeatherType::Clear: {
    // Check if nighttime: use hour from system time (sunset ~18:00, sunrise ~6:00)
    SYSTEMTIME st;
    GetLocalTime(&st);
    bool isNight = (st.wHour < 6 || st.wHour >= 18);

    if (isNight) {
        // Night: crescent moon + twinkling stars
        // Crescent moon: filled white circle - offset dark circle
        float moonR = r * 0.55f;
        float moonX = cx - r * 0.1f;
        float moonY = cy - r * 0.05f;
        float holeOffsetX = r * 0.35f;
        float holeR = moonR * 0.85f;
        // Draw moon (white circle)
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(moonX, moonY), moonR, moonR), m_whiteBrush.Get());
        // Cut crescent (dark circle offset)
        ComPtr<ID2D1SolidColorBrush> darkBrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E, 1.0f), &darkBrush);
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(moonX + holeOffsetX, moonY - r * 0.05f), holeR, holeR), darkBrush.Get());

        // Stars: 3-4 small stars that twinkle once then stop
        // Stars fixed positions around moon
        struct Star { float dx, dy, r; };
        Star stars[4] = {
            { -r * 1.2f, -r * 0.8f, r * 0.12f },
            { r * 0.9f, -r * 1.0f, r * 0.10f },
            { r * 1.1f, r * 0.5f, r * 0.08f },
            { -r * 0.8f, r * 0.9f, r * 0.09f },
        };
        // Twinkle duration: 1.5 seconds worth of phase
        float twinkleDur = 1.5f * 3.14159265f;
        float displayPhase = (m_weatherAnimPhase < twinkleDur) ? m_weatherAnimPhase : twinkleDur;
        // Each star: twinkle once then stop at reduced opacity
        for (int i = 0; i < 4; i++) {
            float sx = cx + stars[i].dx;
            float sy = cy + stars[i].dy;
            float sr = stars[i].r;
            // Twinkle: sine wave that decays, then stays at 0.3 opacity
            float twinkle = sinf(displayPhase * (1.0f + i * 0.3f));
            // Stars twinkle once (opacity animation) then stay visible
            // Draw 4-pointed star using path geometry
            ComPtr<ID2D1PathGeometry> sg;
            m_d2dFactory->CreatePathGeometry(&sg);
            ComPtr<ID2D1GeometrySink> ssk;
            sg->Open(&ssk);
            // 4-pointed star: center, then tip, then next tip, etc.
            float astep = 3.14159265f / 4.0f;
            float baseAngle = 0.0f;
            ssk->BeginFigure(D2D1::Point2F(sx, sy), D2D1_FIGURE_BEGIN_FILLED);
            for (int p = 0; p < 8; p++) {
                float angle = baseAngle + p * astep;
                float len = (p % 2 == 0) ? sr : sr * 0.3f;
                ssk->AddLine(D2D1::Point2F(sx + cosf(angle) * len, sy + sinf(angle) * len));
            }
            ssk->EndFigure(D2D1_FIGURE_END_CLOSED);
            ssk->Close();
            // Use white brush for stars
            m_d2dContext->FillGeometry(sg.Get(), m_whiteBrush.Get());
        }
    } else {
        // Day: hollow ring sun (existing code)
        float ringOuterR = r * 0.72f;
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), ringOuterR, ringOuterR), m_whiteBrush.Get());
        float ringInnerR = r * 0.35f;
        ComPtr<ID2D1SolidColorBrush> holeBrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0x1E1E1E, 1.0f), &holeBrush);
        m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), ringInnerR, ringInnerR), holeBrush.Get());
        float spinDur = 3.14159265f;
        float displayPhase = (m_weatherAnimPhase < spinDur) ? m_weatherAnimPhase : spinDur;
        float rayLen = r * 0.28f;
        float rayOff = r * 1.08f;
        for (int i = 0; i < 8; i++) {
            float angle = displayPhase + i * 3.14159265f * 2.0f / 8.0f;
            float ca = cosf(angle);
            float sa = sinf(angle);
            D2D1_POINT_2F p1 = D2D1::Point2F(cx + ca * rayOff, cy + sa * rayOff);
            D2D1_POINT_2F p2 = D2D1::Point2F(cx + ca * (rayOff + rayLen), cy + sa * (rayOff + rayLen));
            ComPtr<ID2D1PathGeometry> rg;
            m_d2dFactory->CreatePathGeometry(&rg);
            ComPtr<ID2D1GeometrySink> sk;
            rg->Open(&sk);
            sk->BeginFigure(p1, D2D1_FIGURE_BEGIN_HOLLOW);
            sk->AddLine(p2);
            sk->EndFigure(D2D1_FIGURE_END_OPEN);
            sk->Close();
            m_d2dContext->DrawGeometry(rg.Get(), m_whiteBrush.Get(), 1.5f);
        }
    }
    break;
}

case WeatherType::PartlyCloudy: {



            float cloudCY = cy + r * 0.1f;



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cloudCY), r * 1.05f, r * 0.5f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.55f, cloudCY - r * 0.22f), r * 0.45f, r * 0.4f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.45f, cloudCY - r * 0.18f), r * 0.4f, r * 0.35f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.25f, cloudCY - r * 0.45f), r * 0.42f, r * 0.42f), m_whiteBrush.Get());



            break;



        }







        case WeatherType::Cloudy:



        case WeatherType::Default: {



            float cloudCY = cy + r * 0.05f;



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cloudCY), r * 1.05f, r * 0.5f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.55f, cloudCY - r * 0.22f), r * 0.45f, r * 0.4f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.45f, cloudCY - r * 0.18f), r * 0.4f, r * 0.35f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.1f, cloudCY - r * 0.42f), r * 0.38f, r * 0.32f), m_whiteBrush.Get());



            break;



        }







        case WeatherType::Rainy: {



            float cloudCY = cy - r * 0.15f;



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cloudCY), r * 1.05f, r * 0.45f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.5f, cloudCY - r * 0.2f), r * 0.4f, r * 0.35f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.4f, cloudCY - r * 0.15f), r * 0.38f, r * 0.32f), m_whiteBrush.Get());



            float dropY = fmodf(m_weatherAnimPhase * r * 0.25f, size * 0.5f);



            float baseY = cloudCY + r * 0.5f;



            for (int i = 0; i < 3; i++) {



                float dx = (i - 1) * r * 0.45f;



                float dy = fmodf(dropY + i * r * 0.2f, size * 0.45f);



                D2D1_ELLIPSE drop = D2D1::Ellipse(D2D1::Point2F(cx + dx, baseY + dy), r * 0.05f, r * 0.15f);



                m_d2dContext->FillEllipse(drop, m_whiteBrush.Get());



            }



            break;



        }







        case WeatherType::Thunder: {



            float cloudCY = cy - r * 0.15f;



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cloudCY), r * 1.05f, r * 0.45f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.5f, cloudCY - r * 0.2f), r * 0.4f, r * 0.35f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.4f, cloudCY - r * 0.15f), r * 0.38f, r * 0.32f), m_whiteBrush.Get());



            float flashPhase = fmodf(m_weatherAnimPhase / 0.8f, 1.0f);



            if (flashPhase < 0.15f) {



                ComPtr<ID2D1PathGeometry> bg;



                m_d2dFactory->CreatePathGeometry(&bg);



                ComPtr<ID2D1GeometrySink> bsk;



                bg->Open(&bsk);



                bsk->BeginFigure(D2D1::Point2F(cx - r * 0.05f, cloudCY + r * 0.45f), D2D1_FIGURE_BEGIN_HOLLOW);



                bsk->AddLine(D2D1::Point2F(cx + r * 0.15f, cloudCY + r * 0.8f));



                bsk->AddLine(D2D1::Point2F(cx - r * 0.1f, cloudCY + r * 0.8f));



                bsk->AddLine(D2D1::Point2F(cx + r * 0.2f, cloudCY + r * 1.2f));



                bsk->EndFigure(D2D1_FIGURE_END_OPEN);



                bsk->Close();



                m_d2dContext->FillGeometry(bg.Get(), m_whiteBrush.Get());



            }



            break;



        }







        case WeatherType::Snow: {



            float cloudCY = cy - r * 0.15f;



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cloudCY), r * 1.05f, r * 0.45f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.5f, cloudCY - r * 0.2f), r * 0.4f, r * 0.35f), m_whiteBrush.Get());



            m_d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.4f, cloudCY - r * 0.15f), r * 0.38f, r * 0.32f), m_whiteBrush.Get());



            for (int i = 0; i < 3; i++) {



                float driftX = sinf(m_weatherAnimPhase * 0.8f + i * 2.1f) * r * 0.2f;



                float fx = cx + (i - 1) * r * 0.45f + driftX;



                float fy = cloudCY + r * 0.45f + fmodf(m_weatherAnimPhase * 0.5f + i * 0.9f, 1.0f) * size * 0.45f;



                float fAngle = m_weatherAnimPhase * 1.5f + i * 2.094f;



                float fSize = r * 0.18f;



                for (int j = 0; j < 6; j++) {



                    float a = fAngle + j * 3.14159f / 3.0f;



                    D2D1_ELLIPSE star = D2D1::Ellipse(



                        D2D1::Point2F(fx + cosf(a) * fSize * 0.55f, fy + sinf(a) * fSize * 0.55f),



                        fSize * 0.35f, fSize * 0.12f);



                    m_d2dContext->FillEllipse(star, m_whiteBrush.Get());



                }



            }



            break;



        }







        case WeatherType::Fog: {



            float lineCY = cy - r * 0.3f;



            float spacing = r * 0.55f;



            for (int i = 0; i < 3; i++) {



                m_whiteBrush->SetOpacity((1.0f - (float)i * 0.28f) * alpha);



                m_d2dContext->FillEllipse(



                    D2D1::Ellipse(D2D1::Point2F(cx, lineCY + i * spacing), r * 1.1f, r * 0.12f), m_whiteBrush.Get());



            }



            m_whiteBrush->SetOpacity(alpha);



            break;



        }



    }



}



