// shadercmp: numerically compare original pixel shaders (.cso) against the HLSL rewrites using D3D11 WARP.
//   shadercmp MANIFEST HLSL_DIR CSO_ROOT [filter-substring]
// Every pixel shader gets identical random inputs (flat interpolants, random textures t0..t15, random cbuffers b0..b3).
// Windows only; a development tool, not part of the engine.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

static const char* kVS = R"(
cbuffer P : register(b0) { float4 v[10]; };
struct O { float4 pos:SV_Position; float4 c0:COLOR0; float4 c1:COLOR1;
  float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;
  float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7; };
O main(uint id : SV_VertexID) {
  O o; float2 p = float2((id << 1) & 2, id & 2);
  o.pos = float4(p * 2 - 1, 0.5, 1); o.pos.y = -o.pos.y;
  o.c0 = v[0]; o.c1 = v[1]; o.t0 = v[2]; o.t1 = v[3]; o.t2 = v[4]; o.t3 = v[5];
  o.t4 = v[6]; o.t5 = v[7]; o.t6 = v[8]; o.t7 = v[9]; return o; })";

static std::vector<uint8_t> read_file(const fs::path& p) {
	std::ifstream f(p, std::ios::binary);
	return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
	if (argc < 4) return 2;
	const fs::path manifest = argv[1], hlsl_dir = argv[2], cso_root = argv[3];
	const std::string filter = argc > 4 ? argv[4] : "";

	ComPtr<ID3D11Device> dev;
	ComPtr<ID3D11DeviceContext> ctx;
	D3D_FEATURE_LEVEL fl;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) {
		std::fputs("no WARP device\n", stderr);
		return 1;
	}
	ComPtr<ID3DBlob> vsb, err;
	D3DCompile(kVS, std::strlen(kVS), "vs", nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsb, &err);
	ComPtr<ID3D11VertexShader> vs;
	dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);

	// 1 KiB covers every pixel-shader cbuffer in this corpus (largest is 512 bytes).
	constexpr int W = 16, H = 16, TEX = 8, CB_FLOATS = 256, TARGETS = 8;
	D3D11_TEXTURE2D_DESC rd{W, H, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET};
	ComPtr<ID3D11Texture2D> rt[TARGETS], stage[TARGETS];
	ComPtr<ID3D11RenderTargetView> rtv[TARGETS];
	for (int i = 0; i < TARGETS; ++i) {
		dev->CreateTexture2D(&rd, nullptr, &rt[i]);
		dev->CreateRenderTargetView(rt[i].Get(), nullptr, &rtv[i]);
		auto stagingDesc = rd;
		stagingDesc.Usage = D3D11_USAGE_STAGING; stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		dev->CreateTexture2D(&stagingDesc, nullptr, &stage[i]);
	}

	D3D11_SAMPLER_DESC sd{};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	ComPtr<ID3D11SamplerState> samp;
	dev->CreateSamplerState(&sd, &samp);
	// Distinct states expose shader paths that sample the same resource through s0/s1/s3.
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	ComPtr<ID3D11SamplerState> pointSamp;
	dev->CreateSamplerState(&sd, &pointSamp);

	std::ifstream mf(manifest);
	std::string line;
	std::getline(mf, line);
	int ok = 0, differ = 0, failed = 0;
	while (std::getline(mf, line)) {
		std::stringstream ss(line);
		std::string name, file, defs;
		std::getline(ss, name, '\t'); std::getline(ss, file, '\t'); std::getline(ss, defs, '\t');
		if (!defs.empty() && defs.back() == '\r') defs.pop_back();
		if (fs::path(name).filename().string().rfind("ps", 0) != 0 || (!filter.empty() && name.find(filter) == std::string::npos)) continue;

		fs::path cso;
		if (name.find('/') != std::string::npos) cso = cso_root / (name + ".cso");
		else for (auto& e : fs::recursive_directory_iterator(cso_root))
			if (e.path().stem() == name && e.path().extension() == ".cso") { cso = e.path(); break; }
		const auto orig = read_file(cso);

		std::vector<std::string> names; std::vector<D3D_SHADER_MACRO> macros;
		std::stringstream ds(defs); std::string d;
		while (std::getline(ds, d, ';')) if (!d.empty()) names.push_back(d);
		for (auto& n : names) macros.push_back({n.c_str(), "1"});
		macros.push_back({nullptr, nullptr});
		ComPtr<ID3DBlob> nb, ne;
		const fs::path src = hlsl_dir / file;
		if (FAILED(D3DCompileFromFile(src.c_str(), macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_4_0", 0, 0, &nb, &ne))) {
			std::printf("COMPILE-FAIL %s %s\n", name.c_str(), ne ? static_cast<const char*>(ne->GetBufferPointer()) : "");
			failed++;
			continue;
		}
		ComPtr<ID3D11PixelShader> ps_o, ps_n;
		if (FAILED(dev->CreatePixelShader(orig.data(), orig.size(), nullptr, &ps_o)) ||
			FAILED(dev->CreatePixelShader(nb->GetBufferPointer(), nb->GetBufferSize(), nullptr, &ps_n))) {
			std::printf("CREATE-FAIL %s\n", name.c_str()); ++failed; continue;
		}

		double worst = 0;
		for (int seed = 0; seed < 24; seed++) {
			std::mt19937 rng(seed * 7919 + 1);
			std::uniform_real_distribution<float> u(0.f, 1.f);
			// Mix of ranges: most seeds in [0,1], a few with wider cbuffer ranges.
			const float lo = seed >= 18 ? -1.f : 0.f, hi = seed >= 18 ? 2.f : 1.f;
			auto rnd = [&] { return lo + (hi - lo) * u(rng); };

			std::vector<float> vsc(40), cbv(4 * CB_FLOATS);
			for (auto& f : vsc) f = u(rng);
			for (auto& f : cbv) f = rnd();
			// Half the scene seeds exercise opaque G-buffer writes instead of alpha discard.
			if (seed % 2 == 0) vsc[3] = 1.0f;
			if (file == "particle.hlsl") {
				const uint32_t enabled = seed % 2;
				std::memcpy(cbv.data(), &enabled, sizeof(enabled));
			}
			auto mkcb = [&](const void* data, UINT bytes) {
				D3D11_BUFFER_DESC bd{bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
				D3D11_SUBRESOURCE_DATA sr{data};
				ComPtr<ID3D11Buffer> b;
				dev->CreateBuffer(&bd, &sr, &b);
				return b;
			};
			ComPtr<ID3D11Buffer> vcb = mkcb(vsc.data(), 160);
			ComPtr<ID3D11Buffer> pcb[4];
			for (int i = 0; i < 4; i++) pcb[i] = mkcb(cbv.data() + i * CB_FLOATS, CB_FLOATS * sizeof(float));

			std::vector<float> tex(TEX * TEX * 4);
			ComPtr<ID3D11ShaderResourceView> srv[16];
			for (int t = 0; t < 16; t++) {
				for (auto& f : tex) f = u(rng);
				if (t == 0 && seed % 2 == 0)
					for (size_t i = 3; i < tex.size(); i += 4) tex[i] = 1.0f;
				if (t == 1 && seed % 6 == 0)
					std::fill(tex.begin(), tex.end(), seed % 12 == 0 ? 0.0f : 1.0f);
				D3D11_TEXTURE2D_DESC td{TEX, TEX, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE};
				D3D11_SUBRESOURCE_DATA sr{tex.data(), TEX * 16};
				ComPtr<ID3D11Texture2D> tx;
				dev->CreateTexture2D(&td, &sr, &tx);
				dev->CreateShaderResourceView(tx.Get(), nullptr, &srv[t]);
			}
			std::vector<float> out[2];
			for (int which = 0; which < 2; which++) {
				const float clear[4] = {0, 0, 0, 0};
				ID3D11RenderTargetView* views[TARGETS];
				for (int i = 0; i < TARGETS; ++i) {
					views[i] = rtv[i].Get(); ctx->ClearRenderTargetView(views[i], clear);
				}
				ctx->OMSetRenderTargets(TARGETS, views, nullptr);
				D3D11_VIEWPORT vp{0, 0, (float)W, (float)H, 0, 1};
				ctx->RSSetViewports(1, &vp);
				ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				ctx->VSSetShader(vs.Get(), nullptr, 0);
				ctx->VSSetConstantBuffers(0, 1, vcb.GetAddressOf());
				ctx->PSSetShader((which ? ps_n : ps_o).Get(), nullptr, 0);
				for (int i = 0; i < 4; i++) ctx->PSSetConstantBuffers(i, 1, pcb[i].GetAddressOf());
				ID3D11ShaderResourceView* sv[16];
				ID3D11SamplerState* ss16[16];
				for (int i = 0; i < 16; i++) { sv[i] = srv[i].Get(); ss16[i] = (i == 1 || i == 3) ? pointSamp.Get() : samp.Get(); }
				ctx->PSSetShaderResources(0, 16, sv);
				ctx->PSSetSamplers(0, 16, ss16);
				ctx->Draw(3, 0);
				for (int target = 0; target < TARGETS; ++target) {
					ctx->CopyResource(stage[target].Get(), rt[target].Get());
					D3D11_MAPPED_SUBRESOURCE m;
					ctx->Map(stage[target].Get(), 0, D3D11_MAP_READ, 0, &m);
					for (int y = 0; y < H; y++) {
						const float* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(m.pData) + y * m.RowPitch);
						out[which].insert(out[which].end(), row, row + W * 4);
					}
					ctx->Unmap(stage[target].Get(), 0);
				}
			}
			for (size_t i = 0; i < out[0].size(); i++) {
				const float a = out[0][i], b = out[1][i];
				if (std::isnan(a) && std::isnan(b)) continue;
				if (std::isinf(a) && a == b) continue;
				const double e = std::fabs(double(a) - b) / std::max(1.0, std::fabs(double(a)));
				worst = std::max(worst, std::isnan(e) ? 1e9 : e);
			}
		}
		if (worst < 1e-4) { ok++; }
		else { differ++; std::printf("DIFF %-48s max-rel-err %.4g\n", name.c_str(), worst); }
	}
	std::printf("match %d, differ %d, compile-failed %d\n", ok, differ, failed);
	return differ || failed ? 1 : 0;
}
