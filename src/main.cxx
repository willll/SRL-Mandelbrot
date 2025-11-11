#include <srl.hpp>
#include <srl_log.hpp> // Logging system

#include <cassert>

// Using to shorten names for Vector and HighColor
using namespace SRL::Types;
using namespace SRL::Math::Types;
using namespace SRL::Logger;

// Constants
static constexpr uint16_t MAX_ITERATIONS = 100;
static constexpr uint16_t WIDTH = SRL::TV::Width;
static constexpr uint16_t HEIGHT = SRL::TV::Height;

/** @brief Color palette management
 *
 * Manages a color palette for the Mandelbrot set visualization.
 * Provides methods for setting and retrieving colors, with bounds checking.
 * The palette is initialized with a gradient of colors for visualization.
 */
class Palette : public SRL::Bitmap::Palette
{
public:
    explicit Palette(size_t count) : SRL::Bitmap::Palette(count) {}

    /** @brief Set a palette entry
     *
     * Stores a color at the given palette index. Logs a fatal error if the
     * index is out of range.
     * @param index Palette index to set
     * @param color Color value to move into the palette
     */
    void SetColor(uint16_t index, HighColor && color)
    {
        if (index < Count)
        {
            Colors[index] = std::move(color);
        }
        else
        {
            Log::LogPrint<LogLevels::FATAL>("index(%d) out of bound", index);
        }
    }

    /** @brief Retrieve a color from the palette
     *
     * Returns the HighColor stored at the given index. If the index is out of
     * bounds a fatal log is emitted and a black color is returned.
     * @param index Palette index to retrieve
     * @return HighColor value at the index (or black on error)
     */
    HighColor GetColor(uint16_t index) const
    {
        if (index < Count)
        {
            return Colors[index];
        }
        Log::LogPrint<LogLevels::FATAL>("index(%d) out of bound", index);
        return HighColor(0, 0, 0);
    }

    /** @brief Initialize the palette with a default gradient
     *
     * Fills the palette with a simple color gradient and sets the final entry
     * to white.
     */
    void Init()
    {
        for (uint8_t i = 0; i < Count - 1; ++i)
        {
            SetColor(i, HighColor::FromRGB555(i, i * 2 % 256, i * 4 % 256));
        }
        SetColor(Count - 1, HighColor::FromRGB555(255, 255, 255));
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
    uint8_t *imageData;
    SRL::Bitmap::BitmapInfo *bitmap;

public:
    /** @brief Construct a canvas
     *
     * Allocates the image buffer and the BitmapInfo that references the palette.
     * @param width Width of the canvas in pixels
     * @param height Height of the canvas in pixels
     * @param palette Palette to be used by the bitmap info
     */
    explicit Canvas(uint16_t width, uint16_t height, Palette &palette)
        : width(width)
        , height(height)
        , imageData(new uint8_t[width * height])
        , bitmap(new SRL::Bitmap::BitmapInfo(width, height, &palette))
    {
    }

    /** @brief Destroy the canvas and free resources */
    ~Canvas()
    {
        delete[] imageData;
        delete bitmap;
    }

    /** @brief Get raw pointer to image data
     *
     * Returns a pointer to the internal 8-bit indexed image buffer.
     */
    uint8_t *GetData() override
    {
        return (uint8_t *)this->imageData;
    }

    /** @brief Set a pixel in the image buffer
     *
     * Writes an 8-bit palette index into the image buffer at (x,y). Bounds
     * are checked before writing.
     */
    void SetPixel(uint16_t x, uint16_t y, uint8_t color)
    {
        if (x < width && y < height)
        {
            this->imageData[y * width + x] = bitmap->Palette->Colors[color];
        }
    }

    /** @brief Get bitmap info for this canvas
     *
     * Returns a copy of the internal BitmapInfo object.
     */
    SRL::Bitmap::BitmapInfo GetInfo() override
    {
        return *bitmap;
    }

    /** @brief Load a palette into CRAM
     *
     * Attempts to find a free CRAM bank, upload the palette and mark the
     * bank as used. Returns the bank id on success or -1 on failure.
     */
    static int16_t LoadPalette(SRL::Bitmap::BitmapInfo* bitmap)
    {
        // Get free CRAM bank
        int32_t id = SRL::CRAM::GetFreeBank(bitmap->ColorMode);

        Log::LogPrint<LogLevels::INFO>("palette (%d) ColorMode : %d", id, bitmap->ColorMode);

        if (id >= 0)
        {
            SRL::CRAM::Palette palette(bitmap->ColorMode, id);

            if (palette.Load((HighColor *)bitmap->Palette->Colors, bitmap->Palette->Count) >= 0)
            {
                // Mark bank as in use
                SRL::CRAM::SetBankUsedState(id, bitmap->ColorMode, true);
                return id;
            }
            else
            {
                Log::LogPrint<LogLevels::FATAL>("palette load failure");
            }
        }
        else{
            Log::LogPrint<LogLevels::FATAL>("palette GetFreeBank failure");
        }

        // No free bank found
        return -1;
    }
};

/** @brief Parameters for Mandelbrot set calculation
 *
 * Structure containing the parameters needed for calculating a point in the Mandelbrot set.
 * Includes complex plane coordinates (real, imaginary) and pixel coordinates (x, y).
 */
template <typename T>
struct MandelbrotParameters
{
    T real;   ///< Real component of complex number
    T imag;   ///< Imaginary component of complex number
    uint16_t x; ///< X coordinate on the canvas
    uint16_t y; ///< Y coordinate on the canvas
};

// Forward declaration of MandelbrotRenderer so SlaveTask can reference it
template <typename RealT>
class MandelbrotRenderer;

template <typename RealT = Fxp>
class SlaveTask : public SRL::Types::ITask
{
public:

    /** @brief Constructor
     *
     * Initializes an empty task ready for parameters to be set.
     */
    SlaveTask() : params(), iteration(0) {}

    /** @brief Execute the task work
     *
     * This is implemented after `MandelbrotRenderer` so the renderer's
     * templated calculate function can be referenced.
     */
    void Do();

    /** @brief Set parameters for the task
     *
     * Copies the MandelbrotParameters into the task so the slave worker
     * can compute the iteration count for that pixel.
     */
    void setMandelbrotRenderer(const MandelbrotParameters<RealT> & _params)
    {
        params = _params;
    }

    /** @brief Retrieve the parameters currently stored in the task
     * @return copy of the stored MandelbrotParameters
     */
    const MandelbrotParameters<RealT> getMandelbrotRenderer() const
    {
        return params;
    }

    /** @brief Get the X coordinate stored in the task parameters */
    uint16_t getCurrentX() const
    {
        return params.x;
    }

    /** @brief Get the Y coordinate stored in the task parameters */
    uint16_t getCurrentY() const
    {
        return params.y;
    }

    /** @brief Return the computed iteration count (if available) */
    uint16_t getIteration() const
    {
        return iteration;
    }

protected:
    MandelbrotParameters<RealT> params;
    uint16_t iteration;
};

/** @brief Mandelbrot set renderer
 *
 * Handles the rendering of the Mandelbrot set fractal.
 * Manages canvas, palette, and provides methods for progressive rendering
 * and display of the fractal on the screen using VDP1.
 */
template <typename RealT = Fxp>
class MandelbrotRenderer
{
private:
    Canvas *canvas;
    Palette *palette;
    int32_t canvasTextureId;

    const RealT minReal = static_cast<RealT>(-2.0);
    const RealT maxReal = static_cast<RealT>(1.0);
    const RealT minImag = static_cast<RealT>(-1.0);
    const RealT maxImag = static_cast<RealT>(1.0);

    uint16_t Width;
    uint16_t Height;

    uint16_t currentY = 0;
    uint16_t currentX = 0;
    bool renderComplete = false;

    SlaveTask<RealT> task;
public:
    /** @brief Construct a MandelbrotRenderer
     *
     * Allocates palette and canvas resources and attempts to load the
     * texture into VDP1. The renderer is ready to progressively render
     * lines after construction.
     */
    MandelbrotRenderer() : canvas(nullptr),
                           palette(nullptr),
                           canvasTextureId(-1),
                           Width(WIDTH),
                           Height(HEIGHT),
                           currentY(0),
                           currentX(0),
                           renderComplete(false),
                           task()
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

        canvasTextureId = SRL::VDP1::TryLoadTexture(canvas, Canvas::LoadPalette);

        if (canvasTextureId < 0)
        {
            Log::LogPrint<LogLevels::FATAL>("canvasTextureId(%d) not loaded", canvasTextureId);
            assert(canvasTextureId = > 0 && "palette allocation error");
        }

        task.ResetTask();
    }

    /** @brief Render one scanline of the Mandelbrot set
     *
     * Progressively computes and writes one horizontal line of the
     * Mandelbrot image to the canvas. The method advances internal
     * scanline state so repeated calls complete the full image.
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
            MandelbrotParameters<RealT> params{
                minReal + currentX * (maxReal - minReal) / (Width - 1),
                minImag + currentY * (maxImag - minImag) / (Height - 1),
                currentX,
                currentY};

                // If a previous slave result is available, use it
                if (task.IsDone())
                {
                    canvas->SetPixel(task.getCurrentX(), task.getCurrentY(), task.getIteration() % 256);
                }

                // Send this work to the slave if possible (ExecuteOnSlave checks ResetTask)
                task.setMandelbrotRenderer(params);
                SRL::Slave::ExecuteOnSlave(task);

                // Also compute locally as a fallback so rendering proceeds immediately
                uint16_t iteration = MandelbrotRenderer<RealT>::calculateMandelbrot(params);
                canvas->SetPixel(currentX, currentY, iteration % 256);
        }
        currentX = 0;
        ++currentY;



        if (currentY >= Height)
        {
            renderComplete = true;
        }
    }

    /** @brief Copy canvas image to VDP1 texture memory via DMA
     *
     * Performs a DMA transfer from the canvas image buffer into the VDP1
     * texture slot previously allocated for this canvas.
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

    /** @brief Draw the current texture to screen using VDP1 sprite
     *
     * Submits a sprite draw call that uses the canvas texture to render the
     * Mandelbrot image on screen.
     */
    void draw() const
    {
        SRL::Scene2D::DrawSprite(canvasTextureId, Vector3D(0.0, 0.0, 500.0));
        Log::LogPrint<LogLevels::TESTING>("draw");
    }

    /** @brief Query whether the renderer finished the full image
     * @return true when all scanlines have been rendered
     */
    bool isComplete() const { return renderComplete; }


    /** @brief Calculate iteration count for a point in the complex plane
     *
     * Iterates z_{n+1} = z_n^2 + c until the magnitude exceeds 2 or the
     * maximum iteration count is reached. Returns the number of iterations
     * performed (useful for coloring).
     * @param params MandelbrotParameters containing the complex coordinate
     * @return iteration count (0..MAX_ITERATIONS)
     */
    static uint16_t calculateMandelbrot(const MandelbrotParameters<RealT> &params)
    {
        uint16_t iteration = 0;
        RealT zReal = params.real;
        RealT zImag = params.imag;
        const RealT two = static_cast<RealT>(2.0);
        const RealT four = static_cast<RealT>(4.0);

        while (iteration < MAX_ITERATIONS)
        {
            RealT zRealTemp = zReal * zReal - zImag * zImag + params.real;
            zImag = two * zReal * zImag + params.imag;
            zReal = zRealTemp;

            if (zReal * zReal + zImag * zImag > four)
            {
                return iteration;
            }
            ++iteration;
        }
        return MAX_ITERATIONS;
    }
};

/** @brief Slave task execution implementation
 *
 * Runs the Mandelbrot iteration calculation for the stored parameters. This
 * implementation is placed after the `MandelbrotRenderer` definition so it
 * can reference the renderer's templated calculate function.
 */
template <typename RealT>
void SlaveTask<RealT>::Do()
{
    iteration = MandelbrotRenderer<RealT>::calculateMandelbrot(params);
}

/** @brief Program entry point
 *
 * Initializes the SRL core, constructs the Mandelbrot renderer and enters the
 * main loop which progressively renders the fractal and draws it to screen.
 */
int main()
{
    static MandelbrotRenderer<Fxp> *g_renderer = nullptr;

    SRL::Core::Initialize(HighColor(0, 0, 0));

    g_renderer = new MandelbrotRenderer<Fxp>();

    assert(g_renderer != nullptr && "Failed to create MandelbrotRenderer");

    // Setup VBlank event
    SRL::Core::OnVblank += []()
    { g_renderer->copyToVDP1(); };
    ;

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
