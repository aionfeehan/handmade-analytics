#include "imgui_draw.cpp"
#include "imgui_tables.cpp"
#include "imgui_widgets.cpp"
#include "imgui.cpp"
#include "imgui_impl_win32.cpp"
#include "imgui_impl_dx12.cpp"
#include "imgui_demo.cpp"

#include "implot_items.cpp"
#include "implot.cpp"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>

#include "yield_curve.cpp"
#include "parse_dtcc.cpp"


#define global_variable static
#define local_persist static
#define internal static

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "D3D12.lib")

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif


struct FrameContext {
	ID3D12CommandAllocator *command_allocator;
	UINT64 fence_value;
};

global_variable int const NUM_FRAMES_IN_FLIGHT = 3;
global_variable FrameContext global_frame_contexts[NUM_FRAMES_IN_FLIGHT] = {};
global_variable UINT global_frame_index = 0;

global_variable int const NUM_BACK_BUFFERS = 3;
global_variable ID3D12Device *global_pd3d_device = nullptr;
global_variable ID3D12DescriptorHeap *global_pd3d_rtv_descriptor_heap = nullptr;
global_variable ID3D12DescriptorHeap *global_pd3d_srv_descriptor_heap = nullptr;
global_variable ID3D12CommandQueue *global_pd3d_command_queue = nullptr;
global_variable ID3D12GraphicsCommandList *global_pd3d_command_list = nullptr;
global_variable ID3D12Fence *global_fence = nullptr;
global_variable HANDLE global_fence_event = nullptr;
global_variable UINT64 global_fence_last_signaled_value = 0;
global_variable IDXGISwapChain3 *global_swapchain_pointer = nullptr;
global_variable HANDLE global_hswapchain_waitable_object = nullptr;
global_variable ID3D12Resource *global_main_render_target_resources[NUM_BACK_BUFFERS] = {};
global_variable D3D12_CPU_DESCRIPTOR_HANDLE global_main_render_target_descriptor[NUM_BACK_BUFFERS] = {};

bool CreateDeviceD3D(HWND window_handle);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext *WaitForNextFrameResources();
LRESULT WINAPI WndProc(HWND window_handle, UINT message, WPARAM window_param, LPARAM param);

int main(int, char *) {
	WNDCLASSEXW window_class = {
		sizeof(window_class),
		CS_CLASSDC,
		WndProc,
		0L, 0L,
		GetModuleHandle(nullptr),
		nullptr, nullptr, nullptr, nullptr,
		L"Toto has an application",
		nullptr
	};
	::RegisterClassExW(&window_class);
	HWND window_handle = ::CreateWindowW(
		window_class.lpszClassName,
		L"Analytics visualizer",
		WS_OVERLAPPEDWINDOW,
		100, 100, 1280, 800,
		nullptr, nullptr,
		window_class.hInstance,
		nullptr
	);

	if (!CreateDeviceD3D(window_handle)) {
		CleanupDeviceD3D();
		::UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
		return 1;
	}

	::ShowWindow(window_handle, SW_SHOWDEFAULT);
	::UpdateWindow(window_handle);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO &input_output = ImGui::GetIO(); (void)input_output;
	input_output.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(window_handle);
	ImGui_ImplDX12_Init(global_pd3d_device, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM, global_pd3d_srv_descriptor_heap,
		global_pd3d_srv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
		global_pd3d_srv_descriptor_heap->GetGPUDescriptorHandleForHeapStart()
	);

	ImFont *font = input_output.Fonts->AddFontFromFileTTF("C:/Users/aionf/Github/gui-demos/imgui/misc/fonts/DroidSans.ttf", 16.0f);
	IM_ASSERT(font != nullptr);

	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	bool done = false;

	size_t n_swaps = 2048;

	size_t number_of_fras = 8;
	size_t number_of_swaps = 5;
	size_t number_of_plot_points = 200;
	float *fra_times_to_maturity = (float *)malloc(number_of_fras * sizeof(float));
	float *forward_rates = (float *)malloc(number_of_fras * sizeof(float));
	float *swap_fixed_payment_schedule = (float *)malloc(number_of_swaps * sizeof(float));
	float *swap_rates = (float *)malloc(number_of_swaps * sizeof(float));
	YieldCurveFloat yield_curve;

	float *plot_maturities = (float *)malloc(number_of_plot_points *sizeof(float));
	float *plot_short_rates = (float *)malloc(number_of_plot_points *sizeof(float));
	float *plot_discount_factors = (float *)malloc(number_of_plot_points *sizeof(float));

	for (size_t i = 0; i < number_of_fras; ++i) {
		fra_times_to_maturity[i] = 0.25 * (i + 1);
		forward_rates[i] = 0.05 + i * 0.005;
	}

	swap_fixed_payment_schedule[0] = 5;
	swap_fixed_payment_schedule[1] = 7;
	swap_fixed_payment_schedule[2] = 10;
	swap_fixed_payment_schedule[3] = 15;
	swap_fixed_payment_schedule[4] = 30;

	for (size_t i = 0; i < number_of_swaps; ++i) {
		swap_rates[i] = 0.05 + i * 0.01;
	}

	for (size_t i = 0; i < number_of_plot_points; ++i) {
		plot_maturities[i] = i;
		plot_short_rates[i] = 0;
		plot_discount_factors[i] = 0;
	}

	YieldCurveSwapF *swap_list = (YieldCurveSwapF *)calloc(number_of_swaps, sizeof(YieldCurveSwapF));
	for (size_t i = 0; i < number_of_swaps; ++i) {
		size_t number_fixed_payments = (size_t)(*(swap_fixed_payment_schedule + i));
		size_t number_floating_payments = 10;
		yieldCurveSwapFInit(swap_list + i, number_fixed_payments, number_floating_payments, swap_rates[i]);
	}
	float slider_width = 200;

	while (!done) {
		MSG message;
		while (::PeekMessage(&message, nullptr, 0U, 0U, PM_REMOVE)) {
			::TranslateMessage(&message);
			::DispatchMessage(&message);
			if (message.message == WM_QUIT) {
				done = true;
			}
		}
		if (done) {
			break;
		}

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		{
			local_persist float f = 0.0f;
			local_persist int counter = 0;

			ImGui::Begin("Yield curve viewer");

			if (ImPlot::BeginPlot("Yield Curve USD")) {
				ImPlot::PlotLine("Short Rate", plot_maturities, plot_short_rates, number_of_plot_points);
				ImPlot::PlotLine("Discount Factors", plot_maturities, plot_discount_factors, number_of_plot_points);
				ImPlot::EndPlot();
			}

			for (size_t fra_idx = 0; fra_idx < number_of_fras; ++fra_idx) {

				char fra_end_label[32];
				char forward_rate_label[32];
				snprintf(fra_end_label, 32, "Fra %i end", fra_idx + 1);
				snprintf(forward_rate_label, 32, "Forward %i", fra_idx + 1);

				ImGui::PushItemWidth(slider_width);
				ImGui::SliderFloat(fra_end_label, fra_times_to_maturity + fra_idx, 0, *swap_fixed_payment_schedule);
				ImGui::SameLine();
				ImGui::SliderFloat(forward_rate_label, forward_rates + fra_idx, -0.02, 0.2);
			}
			float minimal_swap_maturity = fra_times_to_maturity[number_of_fras - 1];

			for (size_t swap_idx = 0; swap_idx < number_of_swaps; ++swap_idx) {
				char swap_end_label[32];
				char swap_rate_label[32];
				snprintf(swap_end_label, 32, "Swap %i end", swap_idx + 1);
				snprintf(swap_rate_label, 32, "Swap %i", swap_idx + 1);

				ImGui::PushItemWidth(slider_width);
				ImGui::SliderFloat(swap_end_label, swap_fixed_payment_schedule + swap_idx, minimal_swap_maturity, 50);
				ImGui::SameLine();
				ImGui::SliderFloat(swap_rate_label, swap_rates + swap_idx, -0.02, 0.2);
				swap_list[swap_idx].swap_rate = *(swap_rates + swap_idx);
				minimal_swap_maturity = swap_fixed_payment_schedule[swap_idx];
			}


			yieldCurveFloatStripFras(&yield_curve, forward_rates, fra_times_to_maturity, number_of_fras);
			yieldCurveFloatStripSwaps(&yield_curve, swap_list, swap_rates, number_of_swaps, number_of_fras);
			float *this_short_rate = yield_curve.short_rates;
			float *this_yield_curve_maturity_time = yield_curve.interpolation_times;
			float this_maturity_time = 0;
			float first_maturity_time = 0;
			float last_maturity_time = yield_curve.interpolation_times[yield_curve.number_of_points - 1];
			float plot_maturity_step = (last_maturity_time - first_maturity_time) / ((float)number_of_plot_points);
			float log_discount_factor = 0;
			for (size_t i = 0; i < number_of_plot_points; ++i) {
				this_maturity_time += plot_maturity_step;
				if (this_maturity_time > *this_yield_curve_maturity_time) {
					++this_yield_curve_maturity_time;
					++this_short_rate;
				}
				log_discount_factor -= *this_short_rate * plot_maturity_step;
				plot_maturities[i] = this_maturity_time;
				plot_short_rates[i] = *this_short_rate;
				plot_discount_factors[i] = expf(log_discount_factor);
			}

			ImGui::SameLine();
			ImGui::Text("Counter = %d", counter);
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / input_output.Framerate, input_output.Framerate);
			ImGui::End();
		}

		ImGui::Render();

		FrameContext *frame_context = WaitForNextFrameResources();
		UINT back_buffer_index = global_swapchain_pointer->GetCurrentBackBufferIndex();
		frame_context->command_allocator->Reset();

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = global_main_render_target_resources[back_buffer_index];
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		global_pd3d_command_list->Reset(frame_context->command_allocator, nullptr);
		global_pd3d_command_list->ResourceBarrier(1, &barrier);

		const float clear_color_with_alpha[4] = {
			clear_color.x * clear_color.w,
			clear_color.y * clear_color.w,
			clear_color.z * clear_color.w,
			clear_color.w
		};
		global_pd3d_command_list->ClearRenderTargetView(
			global_main_render_target_descriptor[back_buffer_index],
			clear_color_with_alpha,
			0, nullptr
		);
		global_pd3d_command_list->OMSetRenderTargets(
			1,
			&global_main_render_target_descriptor[back_buffer_index],
			FALSE, nullptr
		);
		global_pd3d_command_list->SetDescriptorHeaps(1, &global_pd3d_srv_descriptor_heap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), global_pd3d_command_list);
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		global_pd3d_command_list->ResourceBarrier(1, &barrier);
		global_pd3d_command_list->Close();

		global_pd3d_command_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&global_pd3d_command_list);

		global_swapchain_pointer->Present(1, 0);

		UINT64 fence_value = global_fence_last_signaled_value + 1;
		global_pd3d_command_queue->Signal(global_fence, fence_value);
		global_fence_last_signaled_value = fence_value;
		frame_context->fence_value = fence_value;
	}

	free(swap_list);

	WaitForLastSubmittedFrame();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(window_handle);
	::UnregisterClassW(window_class.lpszClassName, window_class.hInstance);

	return 0;
}

bool CreateDeviceD3D(HWND window_handle) {
	DXGI_SWAP_CHAIN_DESC1 swapchain_description;
	{
		ZeroMemory(&swapchain_description, sizeof(swapchain_description));
		swapchain_description.BufferCount = NUM_BACK_BUFFERS;
		swapchain_description.Width = 0;
		swapchain_description.Height = 0;
		swapchain_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapchain_description.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		swapchain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapchain_description.SampleDesc.Count = 1;
		swapchain_description.SampleDesc.Quality = 0;
		swapchain_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapchain_description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapchain_description.Scaling = DXGI_SCALING_STRETCH;
		swapchain_description.Stereo = FALSE;
	}

#ifdef DX12_ENABLE_DEBUG_LAYER
	ID3D12Debug *pdx12Debug = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug)))) {
		pdx12Debug->EnableDebugLayer();
#endif

		D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
		if (D3D12CreateDevice(nullptr, feature_level, IID_PPV_ARGS(&global_pd3d_device)) != S_OK) {
			return false;
		}
	}
#ifdef DX12_ENABLE_DEBUG_LAYER
	if (pdx12Debug != nullptr)
	{
		ID3D12InfoQueue *pInfoQueue = nullptr;
		global_pd3d_device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		pInfoQueue->Release();
		pdx12Debug->Release();
	}
#endif

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		if (global_pd3d_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&global_pd3d_rtv_descriptor_heap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = global_pd3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = global_pd3d_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			global_main_render_target_descriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (global_pd3d_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&global_pd3d_srv_descriptor_heap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (global_pd3d_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&global_pd3d_command_queue)) != S_OK)
			return false;
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (global_pd3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&global_frame_contexts[i].command_allocator)) != S_OK)
			return false;

	if (global_pd3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, global_frame_contexts[0].command_allocator, nullptr, IID_PPV_ARGS(&global_pd3d_command_list)) != S_OK ||
		global_pd3d_command_list->Close() != S_OK)
		return false;

	if (global_pd3d_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&global_fence)) != S_OK)
		return false;

	global_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (global_fence_event == nullptr)
		return false;

	{
		IDXGIFactory4 *dxgiFactory = nullptr;
		IDXGISwapChain1 *swapChain1 = nullptr;
		if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
			return false;
		if (dxgiFactory->CreateSwapChainForHwnd(global_pd3d_command_queue, window_handle, &swapchain_description, nullptr, nullptr, &swapChain1) != S_OK)
			return false;
		if (swapChain1->QueryInterface(IID_PPV_ARGS(&global_swapchain_pointer)) != S_OK)
			return false;
		swapChain1->Release();
		dxgiFactory->Release();
		global_swapchain_pointer->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		global_hswapchain_waitable_object = global_swapchain_pointer->GetFrameLatencyWaitableObject();
	}

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (global_swapchain_pointer) {
		global_swapchain_pointer->SetFullscreenState(false, nullptr);
		global_swapchain_pointer->Release();
		global_swapchain_pointer = nullptr;
	}
	if (global_hswapchain_waitable_object != nullptr) {
		CloseHandle(global_hswapchain_waitable_object);
	}
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (global_frame_contexts[i].command_allocator) {
			global_frame_contexts[i].command_allocator->Release();
			global_frame_contexts[i].command_allocator = nullptr;
		}
	if (global_pd3d_command_queue) {
		global_pd3d_command_queue->Release();
		global_pd3d_command_queue = nullptr;
	}
	if (global_pd3d_command_list) {
		global_pd3d_command_list->Release();
		global_pd3d_command_list = nullptr;
	}
	if (global_pd3d_rtv_descriptor_heap) {
		global_pd3d_rtv_descriptor_heap->Release();
		global_pd3d_rtv_descriptor_heap = nullptr;
	}
	if (global_pd3d_srv_descriptor_heap) {
		global_pd3d_srv_descriptor_heap->Release();
		global_pd3d_srv_descriptor_heap = nullptr;
	}
	if (global_fence) {
		global_fence->Release();
		global_fence = nullptr;
	}
	if (global_fence_event) {
		CloseHandle(global_fence_event);
		global_fence_event = nullptr;
	}
	if (global_pd3d_device) {
		global_pd3d_device->Release();
		global_pd3d_device = nullptr;
	}

#ifdef DX12_ENABLE_DEBUG_LAYER
	IDXGIDebug1 *pDebug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
	{
		pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
		pDebug->Release();
	}
#endif
}

void CreateRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		ID3D12Resource *pBackBuffer = nullptr;
		global_swapchain_pointer->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		global_pd3d_device->CreateRenderTargetView(pBackBuffer, nullptr, global_main_render_target_descriptor[i]);
		global_main_render_target_resources[i] = pBackBuffer;
	}
}

void CleanupRenderTarget()
{
	WaitForLastSubmittedFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (global_main_render_target_resources[i]) {
			global_main_render_target_resources[i]->Release();
			global_main_render_target_resources[i] = nullptr;
		}
}

void WaitForLastSubmittedFrame()
{
	FrameContext *frameCtx = &global_frame_contexts[global_frame_index % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtx->fence_value;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtx->fence_value = 0;
	if (global_fence->GetCompletedValue() >= fenceValue)
		return;

	global_fence->SetEventOnCompletion(fenceValue, global_fence_event);
	WaitForSingleObject(global_fence_event, INFINITE);
}

FrameContext *WaitForNextFrameResources()
{
	UINT nextFrameIndex = global_frame_index + 1;
	global_frame_index = nextFrameIndex;

	HANDLE waitableObjects[] = { global_hswapchain_waitable_object, nullptr };
	DWORD numWaitableObjects = 1;

	FrameContext *frameCtx = &global_frame_contexts[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtx->fence_value;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtx->fence_value = 0;
		global_fence->SetEventOnCompletion(fenceValue, global_fence_event);
		waitableObjects[1] = global_fence_event;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtx;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (global_pd3d_device != nullptr && wParam != SIZE_MINIMIZED)
		{
			WaitForLastSubmittedFrame();
			CleanupRenderTarget();
			HRESULT result = global_swapchain_pointer->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
			assert(SUCCEEDED(result) && "Failed to resize swapchain.");
			CreateRenderTarget();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}


