#include <srl.hpp>

// Using to shorten names for Vector and HighColor
using namespace SRL::Types;
using namespace SRL::Math::Types;

/** @brief Simple canvas
 */
class Canvas : SRL::Bitmap::IBitmap
{
private:
    /** @brief Canvas width
     */
    uint16_t width;

    /** @brief Canvas height
     */
    uint16_t height;

    /** @brief Image data
     */
    HighColor *imageData;

public:
    Canvas(uint16_t width, uint16_t height) : width(width), height(height), imageData(new HighColor[width * height]) {}

    /** @brief Destroy the canvas
     */
    ~Canvas()
    {
        if (this->imageData != nullptr)
        {
            delete this->imageData;
        }
    }

    /** @brief Get image data
     * @return Pointer to image data
     */
    uint8_t *GetData() override
    {
        return (uint8_t *)this->imageData;
    }

    /** @brief Set image data
     * @return Pointer to image data
     */
    void SetData(uint16_t x, uint16_t y, HighColor &&data)
    {
        if (x < this->width && y < this->height)
        {
            this->imageData[y * this->width + x] = std::move(data);
        }
    }

    /** @brief Get image info
     * @return image info
     */
    SRL::Bitmap::BitmapInfo GetInfo() override
    {
        return SRL::Bitmap::BitmapInfo(this->width, this->height);
    }
};

/** @brief Simple canvas
 */
class Palette : SRL::Bitmap::Palette
{

public:
    Palette(size_t count) : SRL::Bitmap::Palette(count) {}

    ~Palette() {}

    /** @brief Get image data
     * @return Pointer to image data
     */
    void SetColor(size_t index, HighColor &&color)
    {
        if (index < Count)
        {
            this->Colors[index] = color;
        }
    }

    /** @brief Get image data
     * @return Pointer to image data
     */
    HighColor GetColor(size_t index)
    {
        if (index < Count)
        {
            return this->Colors[index];
        }
        return HighColor(0, 0, 0);
    }
};

/** @brief Canvas instance
 */
Canvas *canvas = nullptr;

/** @brief Palette instance
 */
Palette *palette = nullptr;

/** @brief Id of the canvas texture in VDP1 sprite RAM heap
 */
int32_t canvasTextureId;

/** @brief Canvas width
 */
constexpr uint16_t width = SRL::TV::Width; // 320

/** @brief Canvas height
 */
constexpr uint16_t height = SRL::TV::Height; // 240

static Fxp minReal = Fxp::Convert(-2.0);
static Fxp maxReal = Fxp::Convert(1.0);
static Fxp minImag = Fxp::Convert(-1.0);
static Fxp maxImag = Fxp::Convert(1.0);
const uint16_t maxIterations = 100;

static uint16_t y = 0;
static uint16_t x = 0;
static uint8_t done = 0;

typedef struct parameter_fixed
{
    Fxp real;
    Fxp imag;
    uint16_t x;
    uint16_t y;
} parameter_fixed;

// Function to check if a point is in the Mandelbrot set
uint16_t isInMandelbrot(parameter_fixed *param)
{
    uint16_t iteration = 0;
    Fxp zReal = param->real;
    Fxp zImag = param->imag;

    while (iteration < maxIterations)
    {
        Fxp zRealTemp = zReal * zReal - zImag * zImag + param->real;
        zImag = 2 * zReal * zImag + param->imag;
        zReal = zRealTemp;

        if (zReal * zReal + zImag * zImag > 4.0)
        {
            return iteration;
        }

        ++iteration;
    }

    return maxIterations;
}

void mandelbrot()
{
    // slavedone = 1;
    // uint32_t timemax = TIM_FRT_MCR_TO_CNT(100000);
    // TIM_FRT_SET_16(0);

    for (; y < height; y++)
    {
        for (; x < width; x++)
        {

            // if(TIM_FRT_CNT_TO_MCR(TIM_FRT_GET_16()) > timemax) {
            //   return;
            // }

            if (done)
            {
                return;
            }

            //   if (slavedone) {
            //     slavedone = 0;
            //     slave_param_fixed.real = minReal + x * (maxReal - minReal) / (width - 1);
            //     slave_param_fixed.imag = minImag + y * (maxImag - minImag) / (height - 1);
            //     slave_param.x = x;
            //     slave_param.y = y;
            //     slSlaveFunc(SlaveTask, (void *)(&slave_param_fixed));

            //   } else {
            parameter_fixed param;
            param.real = minReal + x * (maxReal - minReal) / (width - 1);
            param.imag = minImag + y * (maxImag - minImag) / (height - 1);
            int iteration = isInMandelbrot(&param);
            // slBMPset( x-(X_RESOLUTION>>1), y-(Y_RESOLUTION>>1), palette[iteration % 256] );
            canvas->SetData(x, y, palette->GetColor(iteration % 256));
            //  }
        }
        x = 0;
    }

    done = 0;
}

/** @brief Copy the texture during vblank
 */
void VBlankCopy()
{
    if (canvas != nullptr)
    {
        slDMACopy(canvas->GetData(), SRL::VDP1::Textures[canvasTextureId].GetData(), width * height * sizeof(uint16_t));
        slDMAWait();
    }
}

// Main program entry
int main()
{
    SRL::Core::Initialize(HighColor(0, 0, 0));
    // SRL::Debug::Print(1,1, "Random image generator sample");

    // Initialize canvas in VDP1 sprite ram
    canvas = new Canvas(width, height);
    canvasTextureId = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap *)canvas);

    // Initialize palette
    palette = new Palette(256);
    for (size_t i = 0; i < 255; ++i)
    {
        palette->SetColor(i, HighColor(i, i * 2 % 256, i * 4 % 256));
    }
    palette->SetColor(255, HighColor(255, 255, 255));

    // Setup event to copy the canvas to VDP1 sprite RAM on every vblank
    SRL::Core::OnVblank += VBlankCopy;

    mandelbrot();

    // Main program loop
    while (1)
    {

        // Draw the canvas on screen as VDP1 sprite
        SRL::Scene2D::DrawSprite(canvasTextureId, Vector3D(0.0, 0.0, 500.0));

        SRL::Core::Synchronize();
    }

    return 0;
}
