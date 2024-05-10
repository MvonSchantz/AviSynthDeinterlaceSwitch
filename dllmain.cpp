//------------------------------------------------------------------------------
#include "pch.h"
//------------------------------------------------------------------------------
#define SCENE_THRESHOLD 100000
#define PIXEL_MOVEMENT_THRESHOLD 24
#define INTERLACED_METRIC_THRESHOLD_1 4
#define NUMBER_OF_INTERLACED_1_FRAMES_IN_SCENE 10
#define INTERLACED_METRIC_THRESHOLD_2 8
#define NUMBER_OF_INTERLACED_2_FRAMES_IN_SCENE 2
#define FFTW_WIDTH 17
//------------------------------------------------------------------------------
#define LINE_DOUBLE_MARGIN 4
//#define LINE_DOUBLED_METRIC_THRESHOLD_1 20000
#define LINE_DOUBLED_METRIC_THRESHOLD_1 400
#define NUMBER_OF_LINE_DOUBLED_1_FRAMES_IN_SCENE 50
//#define LINE_DOUBLED_METRIC_THRESHOLD_2 50000
#define LINE_DOUBLED_METRIC_THRESHOLD_2 800
#define NUMBER_OF_LINE_DOUBLED_2_FRAMES_IN_SCENE 25
//------------------------------------------------------------------------------
#define ANALYZE_SCENE 1
//------------------------------------------------------------------------------



//------------------------------------------------------------------------------
class CDeinterlaceSwitch : public GenericVideoFilter
{
public:
	CDeinterlaceSwitch(PClip clip, PClip progressiveClip, PClip deinterlacedClip, const bool info = false, const bool visualize = false);
	~CDeinterlaceSwitch() override;
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override;

private:

	static void YUV2RGB_Shift5(const int y, const int u, const int v, int& r, int& g, int& b)
	{
		r = ((1192 * (y - 16) + 1634 * (v - 128)) / 1024) >> 5;
		g = ((1192 * (y - 16) - 833 * (v - 128) - 400 * (u - 128)) / 1024) >> 5;
		b = ((1192 * (y - 16) + 2066 * (u - 128)) / 1024) >> 5;

		if (r > 7)
			r = 7;
		if (g > 7)
			g = 7;
		if (b > 7)
			b = 7;
		if (r < 0)
			r = 0;
		if (g < 0)
			g = 0;
		if (b < 0)
			b = 0;
	}

	// rec601
	static int YUV2RGBBinning(const int y, const int u, const int v)
	{
		int r = ((1192 * (y - 16) + 1634 * (v - 128)) / 1024) >> 5;
		int g = ((1192 * (y - 16) - 833 * (v - 128) - 400 * (u - 128)) / 1024) >> 5;
		int b = ((1192 * (y - 16) + 2066 * (u - 128)) / 1024) >> 5;

		if (r > 7)
			r = 7;
		if (g > 7)
			g = 7;
		if (b > 7)
			b = 7;
		if (r < 0)
			r = 0;
		if (g < 0)
			g = 0;
		if (b < 0)
			b = 0;

		return r * 64 + g * 8 + b;
	}

	bool IsFrameInterlaced(const int n, IScriptEnvironment* env);
	void AnalyzeScene(const int n, IScriptEnvironment* env);
	int GetFirstFrameInScene(const int n, IScriptEnvironment* env);
	int GetLastFrameInScene(const int n, IScriptEnvironment* env);
	__int64	GetFrameDifference(const int n, IScriptEnvironment* env);
	void GetHistograms(const int n1, const int n2, int** hist1, int** hist2, IScriptEnvironment* env);
	int* CalculateHistogram(const int n, int histogram[8 * 8 * 8], IScriptEnvironment* env) const;
	int GetNumberOfInterlacedFramesInScene1(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env);
	int GetNumberOfInterlacedFramesInScene2(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env);
	int GetNumberOfInterlacedFramesInScene(const int firstFrame, const int lastFrame, const int threshold, IScriptEnvironment* env);
	int GetInterlaceMetric(const int n, BYTE* const visualizeFrame, const int visualizePitch, IScriptEnvironment* env);
	
private:
	const PClip progressiveClip;
	const PClip deinterlacedClip;
	const bool showInfo;
	const bool visualize;
	
	__int64* frameDifferences;
	int* interlaceMetric;
	int* firstFrameInScene;
	int* lastFrameInScene;
	int* interlacedFramesInScene1;
	int* interlacedFramesInScene2;
	bool* isInterlaced;
	bool* isAnalyzed;

	int histogram1[8 * 8 * 8];
	int histogram2[8 * 8 * 8];
	int histogram1Frame;
	int histogram2Frame;

	float* fftwInput;
	fftwf_complex* fftwOutput;
	fftwf_plan fftwPlan;

	float** ompFftwInput;
	fftwf_complex** ompFftwOutput;
	fftwf_plan* ompFftwPlan;
};
//------------------------------------------------------------------------------
CDeinterlaceSwitch::CDeinterlaceSwitch(PClip clip, PClip progressiveClip, PClip deinterlacedClip, const bool info, const bool visualize)
	: GenericVideoFilter(clip), progressiveClip(progressiveClip), deinterlacedClip(deinterlacedClip), showInfo(info), visualize(visualize), isAnalyzed(nullptr), histogram1Frame(-1), histogram2Frame(-1)
{
	if (!vi.IsYV12())
	{
		MessageBoxA(nullptr, "At the moment the DeinterlaceSwitch filter only supports the YV12 color format.", "Error", MB_OK | MB_ICONSTOP);
		return;
	}
	
	frameDifferences = new __int64[vi.num_frames];
	interlaceMetric = new int[vi.num_frames];
	firstFrameInScene = new int[vi.num_frames];
	lastFrameInScene = new int[vi.num_frames];
	interlacedFramesInScene1 = new int[vi.num_frames];
	interlacedFramesInScene2 = new int[vi.num_frames];
	isInterlaced = new bool[vi.num_frames];
	isAnalyzed = new bool[vi.num_frames];

	memset(frameDifferences, -1, sizeof(__int64) * vi.num_frames);
	memset(interlaceMetric, -1, sizeof(int) * vi.num_frames);
	memset(firstFrameInScene, -1, sizeof(int) * vi.num_frames);
	memset(lastFrameInScene, -1, sizeof(int) * vi.num_frames);
	memset(interlacedFramesInScene1, -1, sizeof(int) * vi.num_frames);
	memset(interlacedFramesInScene2, -1, sizeof(int) * vi.num_frames);
	memset(isInterlaced, 0, sizeof(bool) * vi.num_frames);
	memset(isAnalyzed, 0, sizeof(bool) * vi.num_frames);

	const int len = FFTW_WIDTH * 2 + 1;
	fftwOutput = static_cast<fftwf_complex*>(_aligned_malloc(sizeof(fftwf_complex) * ((FFTW_WIDTH * 2 + 1) / 2 + 1), 32));
	fftwInput = static_cast<float*>(_aligned_malloc(sizeof(float) * (FFTW_WIDTH * 2 + 1), 32));
	fftwPlan = fftwf_plan_dft_r2c_1d(len, fftwInput, fftwOutput, FFTW_EXHAUSTIVE | FFTW_DESTROY_INPUT);

	ompFftwOutput = static_cast<fftwf_complex**>(_aligned_malloc(sizeof(fftwf_complex*) * vi.height, 32));
	ompFftwInput = static_cast<float**>(_aligned_malloc(sizeof(float*) * vi.height, 32));
	ompFftwPlan = static_cast<fftwf_plan*>(_aligned_malloc(sizeof(fftwf_plan*) * vi.height, 32));

	for (int i = 0; i < vi.height; i++)
	{
		ompFftwOutput[i] = static_cast<fftwf_complex*>(_aligned_malloc(sizeof(fftwf_complex) * ((FFTW_WIDTH * 2 + 1) / 2 + 1), 32));
		ompFftwInput[i] = static_cast<float*>(_aligned_malloc(sizeof(float) * (FFTW_WIDTH * 2 + 1), 32));
		ompFftwPlan[i] = fftwf_plan_dft_r2c_1d(len, ompFftwInput[i], ompFftwOutput[i], FFTW_EXHAUSTIVE | FFTW_DESTROY_INPUT);
	}
}
//------------------------------------------------------------------------------
CDeinterlaceSwitch::~CDeinterlaceSwitch()
{
	if (isAnalyzed != nullptr) {
		for (int i = 0; i < vi.height; i++)
		{
			_aligned_free(ompFftwOutput[i]);
			_aligned_free(ompFftwInput[i]);
		}
		_aligned_free(ompFftwPlan);
		_aligned_free(ompFftwInput);
		_aligned_free(ompFftwOutput);

		_aligned_free(fftwInput);
		_aligned_free(fftwOutput);
		delete[] isAnalyzed;
		delete[] isInterlaced;
		delete[] interlacedFramesInScene1;
		delete[] interlacedFramesInScene2;
		delete[] lastFrameInScene;
		delete[] firstFrameInScene;
		delete[] interlaceMetric;
		delete[] frameDifferences;
	}
}
//------------------------------------------------------------------------------
PVideoFrame CDeinterlaceSwitch::GetFrame(int n, IScriptEnvironment* env)
{
	if (!showInfo && !visualize) {
		if (IsFrameInterlaced(n, env))
		{
			return deinterlacedClip->GetFrame(n, env);
		}
		else
		{
			return progressiveClip->GetFrame(n, env);
		}
	}
	else
	{
		const int first = GetFirstFrameInScene(n, env);
		const int last = GetLastFrameInScene(n, env);
		const int length = last - first + 1;
		const int numberOfInterlaced1 = GetNumberOfInterlacedFramesInScene1(n, first, last, env);
		const int percentage1 = (numberOfInterlaced1 * 100) / length;
		const int numberOfInterlaced2 = GetNumberOfInterlacedFramesInScene2(n, first, last, env);
		const int percentage2 = (numberOfInterlaced2 * 100) / length;

		char interlace[32];
		PVideoFrame out;
		if (IsFrameInterlaced(n, env))
		{
			out = deinterlacedClip->GetFrame(n, env);
			strcpy_s(interlace, "Interlaced");
			
		}
		else
		{
			out = progressiveClip->GetFrame(n, env);
			strcpy_s(interlace, "Progressive");
		}

		const auto height = vi.height;
		const auto heightDiv2 = height / 2;
		const auto width = vi.width;
		const auto widthDiv2 = width / 2;

		auto frame = env->NewVideoFrame(vi);
		BYTE* __restrict yDst = frame->GetWritePtr(PLANAR_Y);
		BYTE* __restrict uDst = frame->GetWritePtr(PLANAR_U);
		BYTE* __restrict vDst = frame->GetWritePtr(PLANAR_V);
		
		const auto yPitch = frame->GetPitch(PLANAR_Y);
		const auto uPitch = frame->GetPitch(PLANAR_U);
		const auto vPitch = frame->GetPitch(PLANAR_V);
		
		if (!visualize) {
			const BYTE* __restrict ySrc = out->GetReadPtr(PLANAR_Y);
			const BYTE* __restrict uSrc = out->GetReadPtr(PLANAR_U);
			const BYTE* __restrict vSrc = out->GetReadPtr(PLANAR_V);
			auto ySrcPitch = out->GetPitch(PLANAR_Y);
			auto uSrcPitch = out->GetPitch(PLANAR_U);
			auto vSrcPitch = out->GetPitch(PLANAR_V);
			
			if (yPitch == ySrcPitch) {
				memcpy(yDst, ySrc, yPitch * height);
			}
			else
			{
				for (int y = 0; y < height; y++, ySrc += ySrcPitch, yDst += yPitch)
				{
					memcpy(yDst, ySrc, vi.width);
				}
			}
			if (uPitch == uSrcPitch && vPitch == vSrcPitch) {
				memcpy(uDst, uSrc, uPitch * heightDiv2);
				memcpy(vDst, vSrc, vPitch * heightDiv2);
			}
			else {
				for (int y = 0; y < heightDiv2; y++, uSrc += uSrcPitch, uDst += uPitch, vSrc += vSrcPitch, vDst += vPitch)
				{
					memcpy(uDst, uSrc, widthDiv2);
					memcpy(vDst, vSrc, widthDiv2);
				}
			}
		} else
		{
			GetInterlaceMetric(n, yDst, yPitch, env);
			memset(uDst, 128, uPitch * heightDiv2);
			memset(vDst, 128, vPitch * heightDiv2);
		}

		if (showInfo) {
			char str[256];
			sprintf_s(str, "%s\nMetric: %i (%i frames > %i, %i%%, %i > %i, %i%%)\nScene %i, frame difference: %lld", interlace, interlaceMetric[n], numberOfInterlaced1, INTERLACED_METRIC_THRESHOLD_1 - 1, percentage1, numberOfInterlaced2, INTERLACED_METRIC_THRESHOLD_2 - 1, percentage2, first, frameDifferences[n]);
			env->ApplyMessage(&frame, vi, str, 128 + 64, RGB(0, 255, 255), 0, 0);
		}
		
		return frame;
	}
}
//------------------------------------------------------------------------------
bool CDeinterlaceSwitch::IsFrameInterlaced(const int n, IScriptEnvironment* env)
{
	if (isAnalyzed[n])
	{
		return isInterlaced[n];
	}

	AnalyzeScene(n, env);
	return isInterlaced[n];
}
//------------------------------------------------------------------------------
void CDeinterlaceSwitch::AnalyzeScene(const int n, IScriptEnvironment* env)
{
	const int first = GetFirstFrameInScene(n, env);
	const int last = GetLastFrameInScene(n, env);
	const int length = last - first + 1;

	const int numberOfInterlacedFrames1 = GetNumberOfInterlacedFramesInScene1(n, first, last, env);
	if (numberOfInterlacedFrames1 >= NUMBER_OF_INTERLACED_1_FRAMES_IN_SCENE || numberOfInterlacedFrames1 * 20 > length) // 5%
	{
		for (int i = first; i <= last; i++) {
			isInterlaced[i] = true;
			isAnalyzed[i] = true;
		}
		return;
	}

	const int numberOfInterlacedFrames2 = GetNumberOfInterlacedFramesInScene2(n, first, last, env);
	if (numberOfInterlacedFrames2 >= NUMBER_OF_INTERLACED_2_FRAMES_IN_SCENE || numberOfInterlacedFrames2 * 100 > length) // 1%
	{
		for (int i = first; i <= last; i++) {
			isInterlaced[i] = true;
			isAnalyzed[i] = true;
		}
		return;
	}

	if (length == 1 && GetInterlaceMetric(n, nullptr, 0, env) > 0)
	{
		isInterlaced[n] = true;
		isAnalyzed[n] = true;
		return;
	}
	
	for (int i = first; i <= last; i++) {
		isInterlaced[i] = false;
		isAnalyzed[i] = true;
	}
}
//------------------------------------------------------------------------------
int CDeinterlaceSwitch::GetFirstFrameInScene(const int n, IScriptEnvironment* env)
{
	if (firstFrameInScene[n] != -1)
	{
		return firstFrameInScene[n];
	}

	int i = n;
	while (i >= 0)
	{
		if (GetFrameDifference(i, env) > SCENE_THRESHOLD)
		{
			for (int j = i; j <= n; j++)
			{
				firstFrameInScene[j] = i;
			}
			return i;
		}
		i--;
	}
	firstFrameInScene[n] = 0;
	return 0;
}
//------------------------------------------------------------------------------
int CDeinterlaceSwitch::GetLastFrameInScene(const int n, IScriptEnvironment* env)
{
	if (lastFrameInScene[n] != -1)
	{
		return lastFrameInScene[n];
	}

	int i = n + 1;
	while (i < vi.num_frames)
	{
		if (GetFrameDifference(i, env) > SCENE_THRESHOLD)
		{
			for (int j = n; j <= i - 1; j++)
			{
				lastFrameInScene[j] = i - 1;
			}
			return i - 1;
		}

		i++;
	}
	lastFrameInScene[n] = vi.num_frames - 1;
	return vi.num_frames - 1;
}
//------------------------------------------------------------------------------
__int64 CDeinterlaceSwitch::GetFrameDifference(const int n, IScriptEnvironment* env)
{
	if (n == 0)
	{
		frameDifferences[n] = LLONG_MAX;
		return LLONG_MAX;
	}
	if (frameDifferences[n] > -1)
	{
		return frameDifferences[n];
	}

	int* histogram;
	int* prevHistogram;
	GetHistograms(n, n - 1, &histogram, &prevHistogram, env);

	__int64 sumDiff = 0;
	for (int h = 0; h < 8 * 8 * 8; h++)
	{
		sumDiff += abs(histogram[h] - prevHistogram[h]);
	}

	const __int64 normalizedDiff = (sumDiff * 960 * 540) / (vi.width * vi.height / 4);
	frameDifferences[n] = normalizedDiff;
	return normalizedDiff;
}
//------------------------------------------------------------------------------
void CDeinterlaceSwitch::GetHistograms(const int n1, const int n2, int** hist1, int** hist2, IScriptEnvironment* env)
{
	if (histogram1Frame == n1 && histogram2Frame == n2)
	{
		*hist1 = histogram1;
		*hist2 = histogram2;
		return;
	}
	if (histogram1Frame == n2 && histogram2Frame == n1)
	{
		*hist1 = histogram2;
		*hist2 = histogram1;
		return;
	}

	if (n2 == histogram1Frame)
	{
		*hist1 = CalculateHistogram(n1, histogram2, env);
		histogram2Frame = n1;
		*hist2 = histogram1;
		return;
	}
	if (n2 == histogram2Frame)
	{
		*hist1 = CalculateHistogram(n1, histogram1, env);
		histogram1Frame = n1;
		*hist2 = histogram2;
		return;
	}
	
	if (n1 == histogram1Frame)
	{
		*hist1 = histogram1;
		*hist2 = CalculateHistogram(n2, histogram2, env);
		histogram2Frame = n2;
		return;
	}
	if (n1 == histogram2Frame)
	{
		*hist1 = histogram2;
		*hist2 = CalculateHistogram(n2, histogram1, env);
		histogram1Frame = n2;
		return;
	}

	*hist1 = CalculateHistogram(n1, histogram1, env);
	histogram1Frame = n1;
	*hist2 = CalculateHistogram(n2, histogram2, env);
	histogram2Frame = n2;
}
//------------------------------------------------------------------------------
int* CDeinterlaceSwitch::CalculateHistogram(const int n, int* __restrict histogram, IScriptEnvironment* env) const
{
	memset(histogram, 0, 8 * 8 * 8 * sizeof(int));

	const auto frame = child->GetFrame(n, env);
	const BYTE* __restrict yPtr = frame->GetReadPtr(PLANAR_Y);
	const BYTE* __restrict uPtr = frame->GetReadPtr(PLANAR_U);
	const BYTE* __restrict vPtr = frame->GetReadPtr(PLANAR_V);
	const auto yPitch = frame->GetPitch(PLANAR_Y);
	const auto uPitch = frame->GetPitch(PLANAR_U);
	const auto vPitch = frame->GetPitch(PLANAR_V);

	const int height = vi.height / 2;
	const int width = vi.width / 2;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			const auto yAvg = (static_cast<int>(yPtr[x * 2]) + static_cast<int>(yPtr[x * 2 + 1]) + static_cast<int>(yPtr[x * 2 + yPitch]) + static_cast<int>(yPtr[x * 2 + yPitch + 1])) / 4;
			/*const auto u = uPtr[x];
			const auto v = vPtr[x];

			int r, g, b;
			YUV2RGB_Shift5(yAvg, u, v, r, g, b);

			histogram[r * 64 + g * 8 + b]++;*/

			histogram[YUV2RGBBinning(yAvg, uPtr[x], vPtr[x])]++;
		}
		yPtr += yPitch;
		uPtr += uPitch;
		vPtr += vPitch;
	}

	return histogram;
}
//------------------------------------------------------------------------------
int CDeinterlaceSwitch::GetNumberOfInterlacedFramesInScene1(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env)
{
	if (interlacedFramesInScene1[n] != -1)
	{
		return interlacedFramesInScene1[n];
	}

	const int frames = GetNumberOfInterlacedFramesInScene(firstFrame, lastFrame, INTERLACED_METRIC_THRESHOLD_1, env);
	interlacedFramesInScene1[n] = frames;
	return frames;
}
//------------------------------------------------------------------------------
int CDeinterlaceSwitch::GetNumberOfInterlacedFramesInScene2(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env)
{
	if (interlacedFramesInScene2[n] != -1)
	{
		return interlacedFramesInScene2[n];
	}

	const int frames = GetNumberOfInterlacedFramesInScene(firstFrame, lastFrame, INTERLACED_METRIC_THRESHOLD_2, env);
	interlacedFramesInScene2[n] = frames;
	return frames;
}
//------------------------------------------------------------------------------

int CDeinterlaceSwitch::GetNumberOfInterlacedFramesInScene(const int firstFrame, const int lastFrame, const int threshold, IScriptEnvironment* env)
{
	int count = 0;
	for (int n = firstFrame; n <= lastFrame; n++)
	{
		const int metric = GetInterlaceMetric(n, nullptr, 0, env);
		if (metric >= threshold)
		{
			count++;
		}
	}
	return count;
}
//------------------------------------------------------------------------------
int CDeinterlaceSwitch::GetInterlaceMetric(const int n, BYTE* const visualizeFrame, const int visualizePitch, IScriptEnvironment* env)
{
	if (visualizeFrame == nullptr && interlaceMetric[n] != -1)
	{
		return interlaceMetric[n];
	}

	const int width = vi.width;
	const int height = vi.height;
	const bool isFirstFrameInScene = n == GetFirstFrameInScene(n, env);
	const bool isSingleFrameScene = GetLastFrameInScene(n, env) == GetFirstFrameInScene(n, env);

	const auto frame = visualizeFrame != nullptr ? visualizeFrame : new unsigned char[width * height];
	const auto dstPitch = visualizeFrame != nullptr ? visualizePitch : width;
	memset(frame, 0, dstPitch * height);

	const auto frameN = child->GetFrame(n, env);
	BYTE* __restrict yDst = frame;
	const BYTE* __restrict ySrc = frameN->GetReadPtr(PLANAR_Y);
	const int lastOffset = isSingleFrameScene ? 0 : (isFirstFrameInScene ? 1 : -1);
	const BYTE* __restrict yLast = child->GetFrame(n + lastOffset, env)->GetReadPtr(PLANAR_Y);
	const auto srcPitch = frameN->GetPitch(PLANAR_Y);

	// Calculate amount of interlace for the current frame, but only for pixels where there is sufficient movement compared
	// to last frame (or next frame, in case this is the first frame of the scene)

	/*ySrc += srcPitch * (FFTW_WIDTH + 1);
	yLast += srcPitch * (FFTW_WIDTH + 1);
	yDst += dstPitch * (FFTW_WIDTH + 1);*/
#pragma omp parallel for schedule(dynamic,2)
	for (int y = FFTW_WIDTH + 1; y < height - FFTW_WIDTH - 1; y++)
	{
		const BYTE* ySrc = frameN->GetReadPtr(PLANAR_Y) + y * srcPitch;
		const BYTE* yLast = child->GetFrame(n + lastOffset, env)->GetReadPtr(PLANAR_Y) + y * srcPitch;
		BYTE* yDst = frame + y * dstPitch;

		for (int x = 0; x < width; x++)
		{
			const int lastDiff = isSingleFrameScene ? INT_MAX : static_cast<int>(ySrc[x]) - static_cast<int>(yLast[x]);
			if (lastDiff < -PIXEL_MOVEMENT_THRESHOLD || lastDiff > PIXEL_MOVEMENT_THRESHOLD)
			{
				//Calculate the frequencies for a short vertical line around the current pixel.

				ompFftwInput[y][0] = static_cast<float>(ySrc[x - srcPitch * 17]);
				ompFftwInput[y][1] = static_cast<float>(ySrc[x - srcPitch * 16]);
				ompFftwInput[y][2] = static_cast<float>(ySrc[x - srcPitch * 15]);
				ompFftwInput[y][3] = static_cast<float>(ySrc[x - srcPitch * 14]);
				ompFftwInput[y][4] = static_cast<float>(ySrc[x - srcPitch * 13]);
				ompFftwInput[y][5] = static_cast<float>(ySrc[x - srcPitch * 12]);
				ompFftwInput[y][6] = static_cast<float>(ySrc[x - srcPitch * 11]);
				ompFftwInput[y][7] = static_cast<float>(ySrc[x - srcPitch * 10]);
				ompFftwInput[y][8] = static_cast<float>(ySrc[x - srcPitch * 9]);
				ompFftwInput[y][9] = static_cast<float>(ySrc[x - srcPitch * 8]);
				ompFftwInput[y][10] = static_cast<float>(ySrc[x - srcPitch * 7]);
				ompFftwInput[y][11] = static_cast<float>(ySrc[x - srcPitch * 6]);
				ompFftwInput[y][12] = static_cast<float>(ySrc[x - srcPitch * 5]);
				ompFftwInput[y][13] = static_cast<float>(ySrc[x - srcPitch * 4]);
				ompFftwInput[y][14] = static_cast<float>(ySrc[x - srcPitch * 3]);
				ompFftwInput[y][15] = static_cast<float>(ySrc[x - srcPitch * 2]);
				ompFftwInput[y][16] = static_cast<float>(ySrc[x - srcPitch * 1]);
				ompFftwInput[y][17] = static_cast<float>(ySrc[x]);
				ompFftwInput[y][18] = static_cast<float>(ySrc[x + srcPitch * 1]);
				ompFftwInput[y][19] = static_cast<float>(ySrc[x + srcPitch * 2]);
				ompFftwInput[y][20] = static_cast<float>(ySrc[x + srcPitch * 3]);
				ompFftwInput[y][21] = static_cast<float>(ySrc[x + srcPitch * 4]);
				ompFftwInput[y][22] = static_cast<float>(ySrc[x + srcPitch * 5]);
				ompFftwInput[y][23] = static_cast<float>(ySrc[x + srcPitch * 6]);
				ompFftwInput[y][24] = static_cast<float>(ySrc[x + srcPitch * 7]);
				ompFftwInput[y][25] = static_cast<float>(ySrc[x + srcPitch * 8]);
				ompFftwInput[y][26] = static_cast<float>(ySrc[x + srcPitch * 9]);
				ompFftwInput[y][27] = static_cast<float>(ySrc[x + srcPitch * 10]);
				ompFftwInput[y][28] = static_cast<float>(ySrc[x + srcPitch * 11]);
				ompFftwInput[y][29] = static_cast<float>(ySrc[x + srcPitch * 12]);
				ompFftwInput[y][30] = static_cast<float>(ySrc[x + srcPitch * 13]);
				ompFftwInput[y][31] = static_cast<float>(ySrc[x + srcPitch * 14]);
				ompFftwInput[y][32] = static_cast<float>(ySrc[x + srcPitch * 15]);
				ompFftwInput[y][33] = static_cast<float>(ySrc[x + srcPitch * 16]);
				ompFftwInput[y][34] = static_cast<float>(ySrc[x + srcPitch * 17]);
				fftwf_execute(ompFftwPlan[y]);

				//Pick the strength of the highest frequency and store it in the frequency buffer. (Filter to remove pixels with low frequency intensities.)
				const int v = static_cast<int>(ompFftwOutput[y][FFTW_WIDTH][0] * 2.0f);
				if (v > 255)
					yDst[x] = 255;
				else if (v > 64)
					yDst[x] = v;
			}
		}
		/*ySrc += srcPitch;
		yLast += srcPitch;
		yDst += dstPitch;*/
	}

	//Filter the frequency buffer to remove single line artifacts mistaken for interlace, typically horizontal edges of objects.
	yDst = frame + dstPitch;
	for (int y = 1; y < height - 1; y++)
	{
		for (int x = 0; x < width; x++)
		{		
#define VPREV yDst[x - dstPitch]
#define V yDst[x]
#define VNEXT yDst[x + dstPitch]
			if (V <= 32 || VPREV >= 16 || VNEXT >= 16)
			{
				yDst[x] = 0;
			}
#undef VNEXT
#undef V
#undef VPREV
		}
		yDst += dstPitch;
	}

	//Stronger horizontal line filter to remove some more edges.
	yDst = frame + dstPitch * 4;
	for (int y = 4; y < height - 4; y++)
	{
		for (int x = 0; x < width; x++)
		{
			const auto v = yDst[x];
			if (v > 16)
			{
#define VPREV4 yDst[x - dstPitch * 4]
#define VPREV2 yDst[x - dstPitch * 2]
#define VNEXT2 yDst[x + dstPitch * 2]
#define VNEXT4 yDst[x + dstPitch * 4]
				if ((VPREV4 < 16 && VNEXT4 < 16) || (VPREV2 < 16 && VNEXT2 < 16)) {
					yDst[x] = 0;
				}
#undef VNEXT4
#undef VNEXT2
#undef VPREV2
#undef VPREV4
			}
		}
		yDst += dstPitch;
	}

	yDst = frame + dstPitch * 4;
	ySrc = child->GetFrame(n, env)->GetReadPtr(PLANAR_Y);
	yLast = child->GetFrame(n + lastOffset, env)->GetReadPtr(PLANAR_Y);
	__int64 sumInterlace = 0;
	int numMovedPixels = 0;
	for (int y = 4; y < height - 4; y++)
	{
		for (int x = 0; x < width; x++)
		{
			const int lastDiff = isSingleFrameScene ? INT_MAX : static_cast<int>(ySrc[x]) - static_cast<int>(yLast[x]);
			if (lastDiff < -PIXEL_MOVEMENT_THRESHOLD || lastDiff > PIXEL_MOVEMENT_THRESHOLD)
			{
				sumInterlace += static_cast<int>(yDst[x]);
				numMovedPixels++;
			}
		}
		ySrc += srcPitch;
		yLast += srcPitch;
		yDst += dstPitch;
	}

	const int interlace = numMovedPixels > 0 ? static_cast<int>(sumInterlace / numMovedPixels) : 0;

	if (visualizeFrame == nullptr) {
		delete[] frame;
	}

	interlaceMetric[n] = interlace;
	return interlace;
}
//------------------------------------------------------------------------------






//------------------------------------------------------------------------------
class CLineDoubleSwitch : public GenericVideoFilter
{
public:
	CLineDoubleSwitch(PClip clip, PClip progressiveClip, PClip correctedClip, const bool info = false, const bool visualize = false);
	~CLineDoubleSwitch() override;
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override;

private:
	// rec601
	static void YUV2RGB_Shift5(const int y, const int u, const int v, int& r, int& g, int& b)
	{
		r = ((1192 * (y - 16) + 1634 * (v - 128)) / 1024) >> 5;
		g = ((1192 * (y - 16) - 833 * (v - 128) - 400 * (u - 128)) / 1024) >> 5;
		b = ((1192 * (y - 16) + 2066 * (u - 128)) / 1024) >> 5;

		if (r > 7)
			r = 7;
		if (g > 7)
			g = 7;
		if (b > 7)
			b = 7;
		if (r < 0)
			r = 0;
		if (g < 0)
			g = 0;
		if (b < 0)
			b = 0;
	}

	// rec601
	static int YUV2RGBBinning(const int y, const int u, const int v)
	{
		int r = ((1192 * (y - 16) + 1634 * (v - 128)) / 1024) >> 5;
		int g = ((1192 * (y - 16) - 833 * (v - 128) - 400 * (u - 128)) / 1024) >> 5;
		int b = ((1192 * (y - 16) + 2066 * (u - 128)) / 1024) >> 5;

		if (r > 7)
			r = 7;
		if (g > 7)
			g = 7;
		if (b > 7)
			b = 7;
		if (r < 0)
			r = 0;
		if (g < 0)
			g = 0;
		if (b < 0)
			b = 0;

		return r * 64 + g * 8 + b;
	}

	bool IsFrameLineDoubled(const int n, IScriptEnvironment* env);
	void AnalyzeScene(const int n, IScriptEnvironment* env);
	int GetFirstFrameInScene(const int n, IScriptEnvironment* env);
	int GetLastFrameInScene(const int n, IScriptEnvironment* env);
	__int64	GetFrameDifference(const int n, IScriptEnvironment* env);
	void GetHistograms(const int n1, const int n2, int** hist1, int** hist2, IScriptEnvironment* env);
	int* CalculateHistogram(const int n, int histogram[8 * 8 * 8], IScriptEnvironment* env) const;
	int GetNumberOfLineDoubledFramesInScene1(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env);
	int GetNumberOfLineDoubledFramesInScene2(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env);
	int GetNumberOfLineDoubledFramesInScene(const int firstFrame, const int lastFrame, const int threshold, IScriptEnvironment* env);
	int GetLineDoubleMetric(const int n, BYTE* const visualizeFrame, const int visualizePitch, IScriptEnvironment* env);

private:
	const PClip progressiveClip;
	const PClip correctedClip;
	const bool showInfo;
	const bool visualize;

	__int64* frameDifferences;
	int* lineDoubleMetric;
	int* firstFrameInScene;
	int* lastFrameInScene;
	int* lineDoubledFramesInScene1;
	int* lineDoubledFramesInScene2;
	bool* isLineDoubled;
	bool* isAnalyzed;

	int histogram1[8 * 8 * 8];
	int histogram2[8 * 8 * 8];
	int histogram1Frame;
	int histogram2Frame;

	float* fftwInput;
	fftwf_complex* fftwOutput;
	fftwf_plan fftwPlan;

	float** ompFftwInput;
	fftwf_complex** ompFftwOutput;
	fftwf_plan* ompFftwPlan;
};
//------------------------------------------------------------------------------
CLineDoubleSwitch::CLineDoubleSwitch(PClip clip, PClip progressiveClip, PClip correctedClip, const bool info, const bool visualize)
	: GenericVideoFilter(clip), progressiveClip(progressiveClip), correctedClip(correctedClip), showInfo(info), visualize(visualize), isAnalyzed(nullptr), histogram1Frame(-1), histogram2Frame(-1)
{
	if (!vi.IsYV12())
	{
		MessageBoxA(nullptr, "At the moment the LineDoubleSwitch filter only supports the YV12 color format.", "Error", MB_OK | MB_ICONSTOP);
		return;
	}

	frameDifferences = new __int64[vi.num_frames];
	lineDoubleMetric = new int[vi.num_frames];
	firstFrameInScene = new int[vi.num_frames];
	lastFrameInScene = new int[vi.num_frames];
	lineDoubledFramesInScene1 = new int[vi.num_frames];
	lineDoubledFramesInScene2 = new int[vi.num_frames];
	isLineDoubled = new bool[vi.num_frames];
	isAnalyzed = new bool[vi.num_frames];

	memset(frameDifferences, -1, sizeof(__int64) * vi.num_frames);
	memset(lineDoubleMetric, -1, sizeof(int) * vi.num_frames);
	memset(firstFrameInScene, -1, sizeof(int) * vi.num_frames);
	memset(lastFrameInScene, -1, sizeof(int) * vi.num_frames);
	memset(lineDoubledFramesInScene1, -1, sizeof(int) * vi.num_frames);
	memset(lineDoubledFramesInScene2, -1, sizeof(int) * vi.num_frames);
	memset(isLineDoubled, 0, sizeof(bool) * vi.num_frames);
	memset(isAnalyzed, 0, sizeof(bool) * vi.num_frames);

	const int len = FFTW_WIDTH * 2 + 1;
	fftwOutput = static_cast<fftwf_complex*>(_aligned_malloc(sizeof(fftwf_complex) * ((FFTW_WIDTH * 2 + 1) / 2 + 1), 32));
	fftwInput = static_cast<float*>(_aligned_malloc(sizeof(float) * (FFTW_WIDTH * 2 + 1), 32));
	fftwPlan = fftwf_plan_dft_r2c_1d(len, fftwInput, fftwOutput, FFTW_EXHAUSTIVE | FFTW_DESTROY_INPUT);

	ompFftwOutput = static_cast<fftwf_complex**>(_aligned_malloc(sizeof(fftwf_complex*) * vi.height, 32));
	ompFftwInput = static_cast<float**>(_aligned_malloc(sizeof(float*) * vi.height, 32));
	ompFftwPlan = static_cast<fftwf_plan*>(_aligned_malloc(sizeof(fftwf_plan*) * vi.height, 32));

	for (int i = 0; i < vi.height; i++)
	{
		ompFftwOutput[i] = static_cast<fftwf_complex*>(_aligned_malloc(sizeof(fftwf_complex) * ((FFTW_WIDTH * 2 + 1) / 2 + 1), 32));
		ompFftwInput[i] = static_cast<float*>(_aligned_malloc(sizeof(float) * (FFTW_WIDTH * 2 + 1), 32));
		ompFftwPlan[i] = fftwf_plan_dft_r2c_1d(len, ompFftwInput[i], ompFftwOutput[i], FFTW_EXHAUSTIVE | FFTW_DESTROY_INPUT);
	}
}
//------------------------------------------------------------------------------
CLineDoubleSwitch::~CLineDoubleSwitch()
{
	if (isAnalyzed != nullptr) {
		for (int i = 0; i < vi.height; i++)
		{
			_aligned_free(ompFftwOutput[i]);
			_aligned_free(ompFftwInput[i]);
		}
		_aligned_free(ompFftwPlan);
		_aligned_free(ompFftwInput);
		_aligned_free(ompFftwOutput);

		_aligned_free(fftwInput);
		_aligned_free(fftwOutput);
		delete[] isAnalyzed;
		delete[] isLineDoubled;
		delete[] lineDoubledFramesInScene1;
		delete[] lineDoubledFramesInScene2;
		delete[] lastFrameInScene;
		delete[] firstFrameInScene;
		delete[] lineDoubleMetric;
		delete[] frameDifferences;
	}
}
//------------------------------------------------------------------------------
PVideoFrame CLineDoubleSwitch::GetFrame(int n, IScriptEnvironment* env)
{
	if (!showInfo && !visualize) {
		if (IsFrameLineDoubled(n, env))
		{
			return correctedClip->GetFrame(n, env);
		}
		else
		{
			return progressiveClip->GetFrame(n, env);
		}
	}
	else
	{
#if ANALYZE_SCENE
		const int first = GetFirstFrameInScene(n, env);
		const int last = GetLastFrameInScene(n, env);
		const int length = last - first + 1;

		const int numberOfLineDoubled1 = GetNumberOfLineDoubledFramesInScene1(n, first, last, env);
		const int percentage1 = (numberOfLineDoubled1 * 100) / length;
		const int numberOfLineDoubled2 = GetNumberOfLineDoubledFramesInScene2(n, first, last, env);
		const int percentage2 = (numberOfLineDoubled2 * 100) / length;
#else
		const int first = n;
		const int last = n;
		const int length = last - first + 1;

		const int numberOfInterlaced1 = 0;
		const int percentage1 = (numberOfInterlaced1 * 100) / length;
		const int numberOfInterlaced2 = 0;
		const int percentage2 = (numberOfInterlaced2 * 100) / length;
#endif

		char interlace[32];
		PVideoFrame out;
#if ANALYZE_SCENE
		if (IsFrameLineDoubled(n, env))
		{
			out = correctedClip->GetFrame(n, env);
			strcpy_s(interlace, "Line Doubled");

		}
		else
		{
			out = progressiveClip->GetFrame(n, env);
			strcpy_s(interlace, "Progressive");
		}
#else
		out = progressiveClip->GetFrame(n, env);
		strcpy_s(interlace, "");
#endif

		const auto height = vi.height;
		const auto heightDiv2 = height / 2;
		const auto width = vi.width;
		const auto widthDiv2 = width / 2;

		auto frame = env->NewVideoFrame(vi);
		BYTE* __restrict yDst = frame->GetWritePtr(PLANAR_Y);
		BYTE* __restrict uDst = frame->GetWritePtr(PLANAR_U);
		BYTE* __restrict vDst = frame->GetWritePtr(PLANAR_V);

		const auto yPitch = frame->GetPitch(PLANAR_Y);
		const auto uPitch = frame->GetPitch(PLANAR_U);
		const auto vPitch = frame->GetPitch(PLANAR_V);

		if (!visualize) {
			const BYTE* __restrict ySrc = out->GetReadPtr(PLANAR_Y);
			const BYTE* __restrict uSrc = out->GetReadPtr(PLANAR_U);
			const BYTE* __restrict vSrc = out->GetReadPtr(PLANAR_V);
			auto ySrcPitch = out->GetPitch(PLANAR_Y);
			auto uSrcPitch = out->GetPitch(PLANAR_U);
			auto vSrcPitch = out->GetPitch(PLANAR_V);

			if (yPitch == ySrcPitch) {
				memcpy(yDst, ySrc, yPitch * height);
			}
			else
			{
				for (int y = 0; y < height; y++, ySrc += ySrcPitch, yDst += yPitch)
				{
					memcpy(yDst, ySrc, vi.width);
				}
			}
			if (uPitch == uSrcPitch && vPitch == vSrcPitch) {
				memcpy(uDst, uSrc, uPitch * heightDiv2);
				memcpy(vDst, vSrc, vPitch * heightDiv2);
			}
			else {
				for (int y = 0; y < heightDiv2; y++, uSrc += uSrcPitch, uDst += uPitch, vSrc += vSrcPitch, vDst += vPitch)
				{
					memcpy(uDst, uSrc, widthDiv2);
					memcpy(vDst, vSrc, widthDiv2);
				}
			}
		}
		else
		{
			GetLineDoubleMetric(n, yDst, yPitch, env);
			memset(uDst, 128, uPitch * heightDiv2);
			memset(vDst, 128, vPitch * heightDiv2);
		}

		if (showInfo) {
			char str[256];
			sprintf_s(str, "%s\nMetric: %i (%i frames > %i, %i%%, %i > %i, %i%%)\nScene %i, frame difference: %lld", interlace, lineDoubleMetric[n], numberOfLineDoubled1, LINE_DOUBLED_METRIC_THRESHOLD_1 - 1, percentage1, numberOfLineDoubled2, LINE_DOUBLED_METRIC_THRESHOLD_2 - 1, percentage2, first, frameDifferences[n]);
			env->ApplyMessage(&frame, vi, str, 128 + 64, RGB(0, 255, 255), 0, 0);
		}

		return frame;
	}
}
//------------------------------------------------------------------------------
bool CLineDoubleSwitch::IsFrameLineDoubled(const int n, IScriptEnvironment* env)
{
	if (isAnalyzed[n])
	{
		return isLineDoubled[n];
	}

	AnalyzeScene(n, env);
	return isLineDoubled[n];
}
//------------------------------------------------------------------------------
void CLineDoubleSwitch::AnalyzeScene(const int n, IScriptEnvironment* env)
{
	const int first = GetFirstFrameInScene(n, env);
	const int last = GetLastFrameInScene(n, env);
	const int length = last - first + 1;

	const int numberOfLineDoubledFrames1 = GetNumberOfLineDoubledFramesInScene1(n, first, last, env);
	if (numberOfLineDoubledFrames1 >= NUMBER_OF_LINE_DOUBLED_1_FRAMES_IN_SCENE || numberOfLineDoubledFrames1 * 5 > length * 4) // 80%
	{
		for (int i = first; i <= last; i++) {
			isLineDoubled[i] = true;
			isAnalyzed[i] = true;
		}
		return;
	}

	const int numberOfLineDoubledFrames2 = GetNumberOfLineDoubledFramesInScene2(n, first, last, env);
	if (numberOfLineDoubledFrames2 >= NUMBER_OF_LINE_DOUBLED_2_FRAMES_IN_SCENE || numberOfLineDoubledFrames2 * 2 > length) // 50%
	{
		for (int i = first; i <= last; i++) {
			isLineDoubled[i] = true;
			isAnalyzed[i] = true;
		}
		return;
	}

	for (int i = first; i <= last; i++) {
		isLineDoubled[i] = false;
		isAnalyzed[i] = true;
	}
}
//------------------------------------------------------------------------------
int CLineDoubleSwitch::GetFirstFrameInScene(const int n, IScriptEnvironment* env)
{
	if (firstFrameInScene[n] != -1)
	{
		return firstFrameInScene[n];
	}

	int i = n;
	while (i >= 0)
	{
		if (GetFrameDifference(i, env) > SCENE_THRESHOLD)
		{
			for (int j = i; j <= n; j++)
			{
				firstFrameInScene[j] = i;
			}
			return i;
		}
		i--;
	}
	firstFrameInScene[n] = 0;
	return 0;
}
//------------------------------------------------------------------------------
int CLineDoubleSwitch::GetLastFrameInScene(const int n, IScriptEnvironment* env)
{
	if (lastFrameInScene[n] != -1)
	{
		return lastFrameInScene[n];
	}

	int i = n + 1;
	while (i < vi.num_frames)
	{
		if (GetFrameDifference(i, env) > SCENE_THRESHOLD)
		{
			for (int j = n; j <= i - 1; j++)
			{
				lastFrameInScene[j] = i - 1;
			}
			return i - 1;
		}

		i++;
	}
	lastFrameInScene[n] = vi.num_frames - 1;
	return vi.num_frames - 1;
}
//------------------------------------------------------------------------------
__int64 CLineDoubleSwitch::GetFrameDifference(const int n, IScriptEnvironment* env)
{
	if (n == 0)
	{
		frameDifferences[n] = LLONG_MAX;
		return LLONG_MAX;
	}
	if (frameDifferences[n] > -1)
	{
		return frameDifferences[n];
	}

	int* histogram;
	int* prevHistogram;
	GetHistograms(n, n - 1, &histogram, &prevHistogram, env);

	__int64 sumDiff = 0;
	for (int h = 0; h < 8 * 8 * 8; h++)
	{
		sumDiff += abs(histogram[h] - prevHistogram[h]);
	}

	const __int64 normalizedDiff = (sumDiff * 960 * 540) / (vi.width * vi.height / 4);
	frameDifferences[n] = normalizedDiff;
	return normalizedDiff;
}
//------------------------------------------------------------------------------
void CLineDoubleSwitch::GetHistograms(const int n1, const int n2, int** hist1, int** hist2, IScriptEnvironment* env)
{
	if (histogram1Frame == n1 && histogram2Frame == n2)
	{
		*hist1 = histogram1;
		*hist2 = histogram2;
		return;
	}
	if (histogram1Frame == n2 && histogram2Frame == n1)
	{
		*hist1 = histogram2;
		*hist2 = histogram1;
		return;
	}

	if (n2 == histogram1Frame)
	{
		*hist1 = CalculateHistogram(n1, histogram2, env);
		histogram2Frame = n1;
		*hist2 = histogram1;
		return;
	}
	if (n2 == histogram2Frame)
	{
		*hist1 = CalculateHistogram(n1, histogram1, env);
		histogram1Frame = n1;
		*hist2 = histogram2;
		return;
	}

	if (n1 == histogram1Frame)
	{
		*hist1 = histogram1;
		*hist2 = CalculateHistogram(n2, histogram2, env);
		histogram2Frame = n2;
		return;
	}
	if (n1 == histogram2Frame)
	{
		*hist1 = histogram2;
		*hist2 = CalculateHistogram(n2, histogram1, env);
		histogram1Frame = n2;
		return;
	}

	*hist1 = CalculateHistogram(n1, histogram1, env);
	histogram1Frame = n1;
	*hist2 = CalculateHistogram(n2, histogram2, env);
	histogram2Frame = n2;
}
//------------------------------------------------------------------------------
int* CLineDoubleSwitch::CalculateHistogram(const int n, int* __restrict histogram, IScriptEnvironment* env) const
{
	memset(histogram, 0, 8 * 8 * 8 * sizeof(int));

	const auto frame = child->GetFrame(n, env);
	const BYTE* __restrict yPtr = frame->GetReadPtr(PLANAR_Y);
	const BYTE* __restrict uPtr = frame->GetReadPtr(PLANAR_U);
	const BYTE* __restrict vPtr = frame->GetReadPtr(PLANAR_V);
	const auto yPitch = frame->GetPitch(PLANAR_Y);
	const auto uPitch = frame->GetPitch(PLANAR_U);
	const auto vPitch = frame->GetPitch(PLANAR_V);

	const int height = vi.height / 2;
	const int width = vi.width / 2;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			const auto yAvg = (static_cast<int>(yPtr[x * 2]) + static_cast<int>(yPtr[x * 2 + 1]) + static_cast<int>(yPtr[x * 2 + yPitch]) + static_cast<int>(yPtr[x * 2 + yPitch + 1])) / 4;
			histogram[YUV2RGBBinning(yAvg, uPtr[x], vPtr[x])]++;
		}
		yPtr += yPitch;
		uPtr += uPitch;
		vPtr += vPitch;
	}

	return histogram;
}
//------------------------------------------------------------------------------
int CLineDoubleSwitch::GetNumberOfLineDoubledFramesInScene1(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env)
{
	if (lineDoubledFramesInScene1[n] != -1)
	{
		return lineDoubledFramesInScene1[n];
	}

	const int frames = GetNumberOfLineDoubledFramesInScene(firstFrame, lastFrame, LINE_DOUBLED_METRIC_THRESHOLD_1, env);
	lineDoubledFramesInScene1[n] = frames;
	return frames;
}
//------------------------------------------------------------------------------
int CLineDoubleSwitch::GetNumberOfLineDoubledFramesInScene2(const int n, const int firstFrame, const int lastFrame, IScriptEnvironment* env)
{
	if (lineDoubledFramesInScene2[n] != -1)
	{
		return lineDoubledFramesInScene2[n];
	}

	const int frames = GetNumberOfLineDoubledFramesInScene(firstFrame, lastFrame, LINE_DOUBLED_METRIC_THRESHOLD_2, env);
	lineDoubledFramesInScene2[n] = frames;
	return frames;
}
//------------------------------------------------------------------------------

int CLineDoubleSwitch::GetNumberOfLineDoubledFramesInScene(const int firstFrame, const int lastFrame, const int threshold, IScriptEnvironment* env)
{
	int count = 0;
	for (int n = firstFrame; n <= lastFrame; n++)
	{
		const int metric = GetLineDoubleMetric(n, nullptr, 0, env);
		if (metric >= threshold)
		{
			count++;
		}
	}
	return count;
}
//------------------------------------------------------------------------------
int CLineDoubleSwitch::GetLineDoubleMetric(const int n, BYTE* const visualizeFrame, const int visualizePitch, IScriptEnvironment* env)
{
	if (visualizeFrame == nullptr && lineDoubleMetric[n] != -1)
	{
		return lineDoubleMetric[n];
	}

	const int width = vi.width;
	const int height = vi.height;
	const bool isFirstFrameInScene = n == GetFirstFrameInScene(n, env);
	const bool isSingleFrameScene = GetLastFrameInScene(n, env) == GetFirstFrameInScene(n, env);

	const auto temp1 = new int[width * height];
	const auto temp2 = new int[width * height];
	const auto frame = visualizeFrame != nullptr ? visualizeFrame : new unsigned char[width * height];
	const auto dstPitch = visualizeFrame != nullptr ? visualizePitch : width;
	const auto tempPitch = width;
	memset(frame, 0, dstPitch * height);
	memset(temp1, 0, tempPitch * height * sizeof(int));
	memset(temp2, 0, tempPitch * height * sizeof(int));

	const auto frameN = child->GetFrame(n, env);
	//BYTE* __restrict yDst = frame;
	//const BYTE* __restrict ySrc = frameN->GetReadPtr(PLANAR_Y);
	const auto srcPitch = frameN->GetPitch(PLANAR_Y);

	// Calculate amount of interlace for the current frame, but only for pixels where there is sufficient movement compared
	// to last frame (or next frame, in case this is the first frame of the scene)

	/*ySrc += srcPitch * (FFTW_WIDTH + 1);
	yDst += dstPitch * (FFTW_WIDTH + 1);*/

	const int halfTemp = (height / 2) * tempPitch;

#pragma omp parallel for schedule(dynamic,2)
	for (int y = FFTW_WIDTH + 1; y < height - FFTW_WIDTH - 1; y++)
	{
		const BYTE* ySrc = frameN->GetReadPtr(PLANAR_Y) + y * srcPitch;
		BYTE* yDst = frame + y * dstPitch;
		int* tmpDst1 = temp1 + y/2 * tempPitch;
		int* tmpDst2 = temp2 + y/2 * tempPitch;

		for (int x = 0; x < width; x++)
		{
			const int metric1 = abs(ySrc[x] - ySrc[x - srcPitch]);
			const int metric2 = abs(ySrc[x] - ySrc[x + srcPitch]);

			// Separate line differences into fields.
			if (y % 2 == 0)
			{
				tmpDst1[x] = metric1;
				tmpDst2[x] = metric2;
			} else
			{
				tmpDst1[x + halfTemp] = metric1;
				tmpDst2[x + halfTemp] = metric2;
			}
			/*tmpDst1[x] = metric1;
			tmpDst2[x] = metric2;*/

			
			//Calculate the frequencies for a short vertical line around the current pixel.

			/*ompFftwInput[y][0] = static_cast<float>(abs(ySrc[x - srcPitch * 17] - ySrc[x - srcPitch * 18]));
			ompFftwInput[y][1] = static_cast<float>(abs(ySrc[x - srcPitch * 16] - ySrc[x - srcPitch * 17]));
			ompFftwInput[y][2] = static_cast<float>(abs(ySrc[x - srcPitch * 15] - ySrc[x - srcPitch * 16]));
			ompFftwInput[y][3] = static_cast<float>(abs(ySrc[x - srcPitch * 14] - ySrc[x - srcPitch * 15]));
			ompFftwInput[y][4] = static_cast<float>(abs(ySrc[x - srcPitch * 13] - ySrc[x - srcPitch * 14]));
			ompFftwInput[y][5] = static_cast<float>(abs(ySrc[x - srcPitch * 12] - ySrc[x - srcPitch * 13]));
			ompFftwInput[y][6] = static_cast<float>(abs(ySrc[x - srcPitch * 11] - ySrc[x - srcPitch * 12]));
			ompFftwInput[y][7] = static_cast<float>(abs(ySrc[x - srcPitch * 10] - ySrc[x - srcPitch * 11]));
			ompFftwInput[y][8] = static_cast<float>(abs(ySrc[x - srcPitch * 9] - ySrc[x - srcPitch * 10]));
			ompFftwInput[y][9] = static_cast<float>(abs(ySrc[x - srcPitch * 8] - ySrc[x - srcPitch * 9]));
			ompFftwInput[y][10] = static_cast<float>(abs(ySrc[x - srcPitch * 7] - ySrc[x - srcPitch * 8]));
			ompFftwInput[y][11] = static_cast<float>(abs(ySrc[x - srcPitch * 6] - ySrc[x - srcPitch * 7]));
			ompFftwInput[y][12] = static_cast<float>(abs(ySrc[x - srcPitch * 5] - ySrc[x - srcPitch * 6]));
			ompFftwInput[y][13] = static_cast<float>(abs(ySrc[x - srcPitch * 4] - ySrc[x - srcPitch * 5]));
			ompFftwInput[y][14] = static_cast<float>(abs(ySrc[x - srcPitch * 3] - ySrc[x - srcPitch * 4]));
			ompFftwInput[y][15] = static_cast<float>(abs(ySrc[x - srcPitch * 2] - ySrc[x - srcPitch * 3]));
			ompFftwInput[y][16] = static_cast<float>(abs(ySrc[x - srcPitch * 1] - ySrc[x - srcPitch * 2]));
			ompFftwInput[y][17] = static_cast<float>(abs(ySrc[x] - ySrc[x - srcPitch * 1]));
			ompFftwInput[y][18] = static_cast<float>(abs(ySrc[x + srcPitch * 1] - ySrc[x]));
			ompFftwInput[y][19] = static_cast<float>(abs(ySrc[x + srcPitch * 2] - ySrc[x + srcPitch * 1]));
			ompFftwInput[y][20] = static_cast<float>(abs(ySrc[x + srcPitch * 3] - ySrc[x + srcPitch * 2]));
			ompFftwInput[y][21] = static_cast<float>(abs(ySrc[x + srcPitch * 4] - ySrc[x + srcPitch * 3]));
			ompFftwInput[y][22] = static_cast<float>(abs(ySrc[x + srcPitch * 5] - ySrc[x + srcPitch * 4]));
			ompFftwInput[y][23] = static_cast<float>(abs(ySrc[x + srcPitch * 6] - ySrc[x + srcPitch * 5]));
			ompFftwInput[y][24] = static_cast<float>(abs(ySrc[x + srcPitch * 7] - ySrc[x + srcPitch * 6]));
			ompFftwInput[y][25] = static_cast<float>(abs(ySrc[x + srcPitch * 8] - ySrc[x + srcPitch * 7]));
			ompFftwInput[y][26] = static_cast<float>(abs(ySrc[x + srcPitch * 9] - ySrc[x + srcPitch * 8]));
			ompFftwInput[y][27] = static_cast<float>(abs(ySrc[x + srcPitch * 10] - ySrc[x + srcPitch * 9]));
			ompFftwInput[y][28] = static_cast<float>(abs(ySrc[x + srcPitch * 11] - ySrc[x + srcPitch * 10]));
			ompFftwInput[y][29] = static_cast<float>(abs(ySrc[x + srcPitch * 12] - ySrc[x + srcPitch * 11]));
			ompFftwInput[y][30] = static_cast<float>(abs(ySrc[x + srcPitch * 13] - ySrc[x + srcPitch * 12]));
			ompFftwInput[y][31] = static_cast<float>(abs(ySrc[x + srcPitch * 14] - ySrc[x + srcPitch * 13]));
			ompFftwInput[y][32] = static_cast<float>(abs(ySrc[x + srcPitch * 15] - ySrc[x + srcPitch * 14]));
			ompFftwInput[y][33] = static_cast<float>(abs(ySrc[x + srcPitch * 16] - ySrc[x + srcPitch * 15]));
			ompFftwInput[y][34] = static_cast<float>(abs(ySrc[x + srcPitch * 17] - ySrc[x + srcPitch * 16]));
			fftwf_execute(ompFftwPlan[y]);

			//Pick the strength of the highest frequency and store it in the frequency buffer. (Filter to remove pixels with low frequency intensities.)
			const int v = static_cast<int>(ompFftwOutput[y][FFTW_WIDTH][0] * 1.0f);
			if (y < FFTW_WIDTH + 4)
				yDst[x] = 0;
			else if (v > 255)
				yDst[x] = 255;
			else if (v > 128)
				yDst[x] = v;*/
		}
		//ySrc += srcPitch;
		//ySrc = frameN->GetReadPtr(PLANAR_Y) + y * srcPitch;
		//yDst += dstPitch;
		//yDst = frame + y * dstPitch;
	}

	const int halfFrame = (height / 2) * dstPitch;

	// Sum the fields.
	int* tmpDest = temp2;
	int* tmpSrc = temp1;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			if (y < height / 2)
				tmpDest[x] = tmpDest[x] + tmpSrc[x + halfTemp];
			else
				tmpDest[x] = tmpDest[x] + tmpSrc[x - halfTemp];
		}

		tmpDest += tempPitch;
		tmpSrc += tempPitch;
	}

#define FIELD_THRESHOLD 16

	// Create preview.
	BYTE* yDst = frame;
	int* ySrc = temp2;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			if (ySrc[x]*4 > 255)
				yDst[x] = 255;
			else if (ySrc[x] < FIELD_THRESHOLD)
				yDst[x] = 0;
			else
				yDst[x] = ySrc[x]*4;

		}
		
		yDst += dstPitch;
		ySrc += tempPitch;
	}

/*#define V yDst[x]
#define VNEXT yDst[x + dstPitch]
	yDst = frame;
	for (int y = 0; y < height - 1; y++)
	{
		for (int x = 0; x < width; x++)
		{
			int vDiff = abs(V - VNEXT);
			int vMax = V > VNEXT ? V : VNEXT;
			V = vMax >= 64 && vDiff > vMax / 2 ? 255 : 0;
			
		}
		yDst += dstPitch;
	}
#undef VNEXT
#undef V*/


	//Filter the frequency buffer to remove single line artifacts mistaken for interlace, typically horizontal edges of objects.
	/*yDst = frame + dstPitch;
	for (int y = 1; y < height - 1; y++)
	{
		for (int x = 0; x < width; x++)
		{
#define VPREV yDst[x - dstPitch]
#define V yDst[x]
#define VNEXT yDst[x + dstPitch]
			if (V <= 32 || VPREV >= 16 || VNEXT >= 16)
			{
				yDst[x] = 0;
			}
#undef VNEXT
#undef V
#undef VPREV
		}
		yDst += dstPitch;
	}*/

	//Stronger horizontal line filter to remove some more edges.
	/*yDst = frame + dstPitch * 4;
	for (int y = 4; y < height - 4; y++)
	{
		for (int x = 0; x < width; x++)
		{
			const auto v = yDst[x];
			if (v > 16)
			{
#define VPREV4 yDst[x - dstPitch * 4]
#define VPREV2 yDst[x - dstPitch * 2]
#define VNEXT2 yDst[x + dstPitch * 2]
#define VNEXT4 yDst[x + dstPitch * 4]
				if ((VPREV4 < 16 && VNEXT4 < 16) || (VPREV2 < 16 && VNEXT2 < 16)) {
					yDst[x] = 0;
				}
#undef VNEXT4
#undef VNEXT2
#undef VPREV2
#undef VPREV4
			}
		}
		yDst += dstPitch;
	}*/

	// Sum field brightness and compare.
	tmpSrc = temp2;
	//ySrc = child->GetFrame(n, env)->GetReadPtr(PLANAR_Y);
	__int64 sum = 0;
	__int64 sumField1 = 0;
	__int64 sumField2 = 0;
	int numMovedPixels = 0;
	for (int y = 0; y < height; y++)
	{
		if (y < height / 2) {
			for (int x = 0; x < width; x++)
			{
				if (tmpSrc[x] >= FIELD_THRESHOLD)
					sumField1 += static_cast<int>(tmpSrc[x]);
			}
		} else
		{
			for (int x = 0; x < width; x++)
			{
				if (tmpSrc[x] >= FIELD_THRESHOLD)
					sumField2 += static_cast<int>(tmpSrc[x]);
			}
		}

		tmpSrc += tempPitch;
	}

	const int lineDoubling1 = sumField2 > 0 ? sumField1 * 100 / sumField2 : 100;
	const int lineDoubling2 = sumField1 > 0 ? sumField2 * 100 / sumField1 : 100;

	const int lineDoubling = lineDoubling1 > lineDoubling2 ? lineDoubling1 : lineDoubling2;

	delete[] temp1;
	delete[] temp2;

	if (visualizeFrame == nullptr) {
		delete[] frame;
	}

	lineDoubleMetric[n] = lineDoubling;
	return lineDoubling;
}
//------------------------------------------------------------------------------






//------------------------------------------------------------------------------
AVSValue __cdecl Create_DeinterlaceSwitch(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	return new CDeinterlaceSwitch(args[0].AsClip(), args[1].AsClip(), args[2].AsClip(), args[3].AsBool(false), args[4].AsBool(false));
}
//------------------------------------------------------------------------------
AVSValue __cdecl Create_LineDoubleSwitch(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	return new CLineDoubleSwitch(args[0].AsClip(), args[1].AsClip(), args[2].AsClip(), args[3].AsBool(false), args[4].AsBool(false));
}
//------------------------------------------------------------------------------
const AVS_Linkage* AVS_linkage = nullptr;
//------------------------------------------------------------------------------
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
	AVS_linkage = vectors;
	env->AddFunction("DeinterlaceSwitch", "c[progressive]c[deinterlaced]c[info]b[visualize]b", Create_DeinterlaceSwitch, nullptr);
	env->AddFunction("LineDoubleSwitch", "c[progressive]c[corrected]c[info]b[visualize]b", Create_LineDoubleSwitch, nullptr);
	return "DeinterlaceSwitch plugin";
}
//------------------------------------------------------------------------------


