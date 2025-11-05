#include <srl.hpp>
#include <srl_log.hpp> // Logging system

#include <cassert>

// Using to shorten names for Vector and HighColor
using namespace SRL::Types;
using namespace SRL::Math::Types;
using namespace SRL::Logger;

// Constants
static constexpr uint16_t MAX_ITERATIONS = 255;
static constexpr uint16_t WIDTH = SRL::TV::Width;
static constexpr uint16_t HEIGHT = SRL::TV::Height;

/** @brief Color palette management
 *
 * Manages a color palette for the Mandelbrot set visualization.
 * Provides methods for setting and retrieving colors, with bounds checking.
 * The palette is initialized with a gradient of colors for visualization.
 */
class Palette
{
private:
    uint16_t Count;
    HighColor *Colors;

public:
    explicit Palette(size_t count) : Count(count), Colors(new HighColor[count]) {}

    ~Palette() { delete[] Colors; }

    void SetColor(uint16_t index, HighColor color)
    {
        if (index < Count)
        {
            Colors[index] = color;
        }
        else
        {
            Log::LogPrint<LogLevels::FATAL>("index(%d) out of bound", index);
        }
    }

    HighColor GetColor(uint16_t index) const
    {
        if (index < Count)
        {
            return Colors[index];
        }
        Log::LogPrint<LogLevels::FATAL>("index(%d) out of bound", index);
        return HighColor(0, 0, 0);
    }

    void Init()
    {
        for (uint16_t i = 0; i < Count - 1; ++i)
        {
            SetColor(i, HighColor(i, i * 2 % 256, i * 4 % 256));
        }
        SetColor(Count - 1, HighColor(255, 255, 255));
    }
};

/** @brief Simple canvas for rendering the Mandelbrot set
 *
 * Canvas class implements a bitmap interface for drawing the Mandelbrot set.
 * It manages a buffer of high color pixels and provides methods for pixel manipulation.
 */
class Canvas : public SRL::Bitmap::IBitmap
{
private:
    uint16_t width;
    uint16_t height;
    HighColor *imageData;
    Palette &palette;

public:
    explicit Canvas(uint16_t width, uint16_t height, Palette &palette)
        : width(width), height(height), imageData(new HighColor[width * height]), palette(palette)
    {
    }

    ~Canvas()
    {
        delete[] imageData;
    }

    uint8_t *GetData() override
    {
        return (uint8_t *)this->imageData;
    }

    void SetPixel(uint16_t x, uint16_t y, uint16_t color)
    {
        if (x < width && y < height)
        {
            imageData[y * width + x] = palette.GetColor(color);
        }
    }

    SRL::Bitmap::BitmapInfo GetInfo() override
    {
        return SRL::Bitmap::BitmapInfo(width, height);
    }
};

/** @brief Parameters for Mandelbrot set calculation
 *
 * Structure containing the parameters needed for calculating a point in the Mandelbrot set.
 * Includes complex plane coordinates (real, imaginary) and pixel coordinates (x, y).
 */
struct MandelbrotParameters
{
    Fxp real;   ///< Real component of complex number
    Fxp imag;   ///< Imaginary component of complex number
    uint16_t x; ///< X coordinate on the canvas
    uint16_t y; ///< Y coordinate on the canvas
};

/** @brief Mandelbrot set renderer
 *
 * Handles the rendering of the Mandelbrot set fractal.
 * Manages canvas, palette, and provides methods for progressive rendering
 * and display of the fractal on the screen using VDP1.
 */
class MandelbrotRenderer
{
private:
    Canvas *canvas;
    Palette *palette;
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
    MandelbrotRenderer() : canvas(nullptr),
                           palette(nullptr),
                           canvasTextureId(-1),
                           Width(WIDTH),
                           Height(HEIGHT),
                           maxIterations(MAX_ITERATIONS),
                           currentY(0),
                           currentX(0),
                           renderComplete(false)
    {
        palette = new Palette(256);
        if (!palette)
        {
            Log::LogPrint<LogLevels::FATAL>("palette allocation error");
            assert(palette != nullptr && "palette allocation error");
        }

        palette->Init();

        canvas = new Canvas(Width, Height, *palette);

        if (canvas == nullptr)
        {
            Log::LogPrint<LogLevels::FATAL>("canvas allocation error");
            assert(canvas != nullptr && "canvas allocation error");
        }

        canvasTextureId = SRL::VDP1::TryLoadTexture(canvas);

        if (canvasTextureId < 0)
        {
            Log::LogPrint<LogLevels::FATAL>("canvasTextureId(%d) not loaded", canvasTextureId);
            assert(canvasTextureId = > 0 && "palette allocation error");
        }
    }

    /** @brief Renders a portion of the Mandelbrot set
     *
     * Progressively renders the Mandelbrot set line by line.
     * Uses a scanline approach for optimization, rendering one line at a time.
     * Updates the canvas with calculated values and manages rendering state.
     */
    void render()
    {
        if (currentY >= Height)
        {
            currentY = 0;
            // renderComplete = false;
        }

        // for (; currentY < HEIGHT && !renderComplete; currentY++) {
        for (currentX = 0; currentX < Width; currentX++)
        {
            MandelbrotParameters params{
                minReal + currentX * (maxReal - minReal) / (Width - 1),
                minImag + currentY * (maxImag - minImag) / (Height - 1),
                currentX,
                currentY};

            uint16_t iteration = calculateMandelbrot(params);
            canvas->SetPixel(currentX, currentY, iteration % 256);
        }
        currentX = 0;
        currentY++;

        //SRL::Debug::Print(1, 2, "Debug %d (%d)", currentY, Height);
        //}
        if (currentY >= Height)
        {
            renderComplete = true;
        }
    }

    /** @brief Copies the rendered image to VDP1 memory
     *
     * Uses DMA to transfer the rendered canvas data to VDP1 texture memory.
     * This operation is synchronized to ensure proper data transfer.
     */
    void copyToVDP1() const
    {
        if (canvas != nullptr)
        {
            slDMACopy(canvas->GetData(),
                      SRL::VDP1::Textures[canvasTextureId].GetData(),
                      Width * Height * sizeof(uint16_t));
            slDMAWait();
        }
        Log::LogPrint<LogLevels::TESTING>("copyToVDP1");
    }

    /** @brief Draws the Mandelbrot set to the screen
     *
     * Renders the current state of the Mandelbrot set using VDP1 sprite drawing.
     * Positions the sprite at the specified coordinates in 3D space.
     */
    void draw() const
    {
        SRL::Scene2D::DrawSprite(canvasTextureId, Vector3D(0.0, 0.0, 500.0));
        Log::LogPrint<LogLevels::TESTING>("draw");
    }

    /** @brief Checks if rendering is complete
     *
     * @return true if the entire Mandelbrot set has been rendered, false otherwise
     */
    bool isComplete() const { return renderComplete; }

private:
    /** @brief Calculates the Mandelbrot set value for a given point
     *
     * Performs the iterative calculation to determine if a point is in the Mandelbrot set.
     * Returns the number of iterations taken before the point escapes, or maxIterations if it's in the set.
     *
     * @param params Parameters containing the complex point coordinates
     * @return Number of iterations before escape, or maxIterations if the point is in the set
     */
    uint16_t calculateMandelbrot(const MandelbrotParameters &params) const
    {
        uint16_t iteration = 0;
        Fxp zReal = params.real;
        Fxp zImag = params.imag;
        const Fxp two = 2.0;
        const Fxp four = 4.0;

        while (iteration < maxIterations)
        {
            Fxp zRealTemp = zReal * zReal - zImag * zImag + params.real;
            zImag = two * zReal * zImag + params.imag;
            zReal = zRealTemp;

            if (zReal * zReal + zImag * zImag > four)
            {
                return iteration;
            }
            ++iteration;
        }
        return maxIterations;
    }
};

int main()
{
    static MandelbrotRenderer *g_renderer = nullptr;

    SRL::Core::Initialize(HighColor(0, 0, 0));

    g_renderer = new MandelbrotRenderer();

    assert(g_renderer != nullptr && "Failed to create MandelbrotRenderer");

    // Setup VBlank event
    SRL::Core::OnVblank += []() { g_renderer->copyToVDP1(); };;

    // Main program loop
    while (true)
    {
        if (!g_renderer->isComplete())
        {
            // Render the Mandelbrot set
            g_renderer->render();
        }

        g_renderer->draw();
        SRL::Core::Synchronize();
    }

    return 0;
}
