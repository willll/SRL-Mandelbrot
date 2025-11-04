#include <srl.hpp>
#include <srl_log.hpp>      // Logging system

#include <cassert>

// Using to shorten names for Vector and HighColor
using namespace SRL::Types;
using namespace SRL::Math::Types;
using namespace SRL::Logger;

// Constants
static constexpr uint16_t MAX_ITERATIONS = 255;
static constexpr uint16_t WIDTH = 24; //SRL::TV::Width;
static constexpr uint16_t HEIGHT = 18; //SRL::TV::Height;


/** @brief Simple canvas for rendering the Mandelbrot set
 */
class Canvas : public SRL::Bitmap::IBitmap {
private:
    uint16_t width;
    uint16_t height;
    HighColor* imageData;

public:
    Canvas(uint16_t width, uint16_t height) 
        : width(width)
        , height(height)
        , imageData(new HighColor[width * height]) {
    }

    ~Canvas() {
        delete[] imageData;
    }

    uint8_t* GetData() override {
        return reinterpret_cast<uint8_t*>(imageData);
    }

    void SetPixel(uint16_t x, uint16_t y, const HighColor& color) {
        if (x < width && y < height) {
            imageData[y * width + x] = color;
        }
    }

    SRL::Bitmap::BitmapInfo GetInfo() const {
        return SRL::Bitmap::BitmapInfo(width, height);
    }
};

/** @brief Color palette management
 */
class Palette {
private:
    uint16_t Count;
    HighColor* Colors;
public:
    explicit Palette(size_t count) : Count(count), Colors(new HighColor[count]) {}
    
    ~Palette() { delete[] Colors; }

    void SetColor(size_t index, HighColor color) {
        if (index < Count) {
            Colors[index] = color;
        } else {
            Log::LogPrint<LogLevels::FATAL>("index(%d) out of bound", index);
        }
    }

    HighColor GetColor(size_t index) const {
        if (index < Count) {
            return Colors[index];
        }
        Log::LogPrint<LogLevels::FATAL>("index(%d) out of bound", index);
        return HighColor(0, 0, 0);
    }

    void Init() {
        for (uint16_t i = 0; i < Count-1; ++i) {
            SetColor(i, HighColor(i, i * 2 % 256, i * 4 % 256));
        }
        SetColor(Count-1, HighColor(255, 255, 255));
    }
};

struct MandelbrotParameters {
    Fxp real;
    Fxp imag;
    uint16_t x;
    uint16_t y;
};

class MandelbrotRenderer {
private:
    Canvas* canvas;
    Palette* palette;
    int32_t canvasTextureId;
    
    const Fxp minReal = -2.0;
    const Fxp maxReal = 1.0;
    const Fxp minImag = -1.0;
    const Fxp maxImag = 1.0;

    uint16_t maxIterations;
    uint16_t Width;
    uint16_t Height;

    uint16_t currentY = 0;
    uint16_t currentX = 0;
    bool renderComplete = false;


public:
    MandelbrotRenderer() :
        canvas(nullptr),
        palette(nullptr),
        canvasTextureId(-1),
        Width(WIDTH),
        Height(HEIGHT),
        maxIterations(MAX_ITERATIONS),
        currentY(0),
        currentX(0),
        renderComplete(false)
    {
        canvas = new Canvas(Width, Height);

        if (canvas == nullptr) {
            Log::LogPrint<LogLevels::FATAL>("canvas allocation error");
            assert(canvas != nullptr && "canvas allocation error");
        }

        palette = new Palette(256);
        if (!palette) {
            Log::LogPrint<LogLevels::FATAL>("palette allocation error");
            assert(palette != nullptr && "palette allocation error"); 
        }

        palette->Init();

        canvasTextureId = SRL::VDP1::TryLoadTexture(canvas);

        if (canvasTextureId < 0) {
            Log::LogPrint<LogLevels::FATAL>("canvasTextureId(%d) not loaded", canvasTextureId);
            assert(canvasTextureId => 0 && "palette allocation error"); 
        }
    }

    void render() {
        if( currentY >= Height)
        {
            currentY=0;
            //renderComplete = false;
        }

        //for (; currentY < HEIGHT && !renderComplete; currentY++) {
            for (; currentX < Width; currentX++) {
                MandelbrotParameters params{
                    minReal + currentX * (maxReal - minReal) / (Width - 1),
                    minImag + currentY * (maxImag - minImag) / (Height - 1),
                    currentX,
                    currentY
                };

                uint16_t iteration = calculateMandelbrot(params);
                canvas->SetPixel(currentX, currentY, palette->GetColor(iteration % 256));
                Log::LogPrint<LogLevels::TESTING>("Itr = %d", iteration);
            }
            currentX = 0;
            currentY++;

            SRL::Debug::Print(1,2, "Debug %d (%d)", currentY, Height);
        //}
        if( currentY >= Height) {
            renderComplete = true;
        }
            
    }

    void copyToVDP1() const {
        if (canvas) {
            slDMACopy(canvas->GetData(), 
                     SRL::VDP1::Textures[canvasTextureId].GetData(), 
                     Width * Height * sizeof(uint16_t));
            slDMAWait();
        }
        Log::LogPrint<LogLevels::TESTING>("copyToVDP1");
    }

    void draw() const {
        SRL::Scene2D::DrawSprite(canvasTextureId, Vector3D(0.0, 0.0, 500.0));
        Log::LogPrint<LogLevels::TESTING>("draw");
    }

    bool isComplete() const { return renderComplete; }

private :

    uint16_t calculateMandelbrot(const MandelbrotParameters& params) const {
        uint16_t iteration = 0;
        Fxp zReal = params.real;
        Fxp zImag = params.imag;
        const Fxp two = 2.0;
        const Fxp four = 4.0;

        while (iteration < maxIterations) {
            Fxp zRealTemp = zReal * zReal - zImag * zImag + params.real;
            zImag = two * zReal * zImag + params.imag;
            zReal = zRealTemp;

            if (zReal * zReal + zImag * zImag > four) {
                return iteration;
            }
            ++iteration;
        }
        return maxIterations;
    }
};

int main() {
    static MandelbrotRenderer* g_renderer = nullptr;

    SRL::Core::Initialize(HighColor(0, 0, 0));
    
    g_renderer = new MandelbrotRenderer();

    assert(g_renderer != nullptr && "Failed to create MandelbrotRenderer");

    // Setup VBlank event
    SRL::Core::OnVblank += []() { g_renderer->copyToVDP1(); };

    // Main program loop
    while (true) {
        SRL::Debug::Print(1,1, "Debug Print sample");
        

        // Render the Mandelbrot set
        g_renderer->render();

        g_renderer->draw();
        SRL::Core::Synchronize();
    }

    return 0;
}
