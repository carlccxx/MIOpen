/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#include "fusionHost.hpp"
#include <miopen/stringutils.hpp>

using ptr_FusionPlanDesc = MIOPEN_MANAGE_PTR(miopenFusionPlanDescriptor_t, miopenDestroyFusionPlan);
using ptr_FusionPlanArgs = MIOPEN_MANAGE_PTR(miopenOperatorArgs_t, miopenDestroyOperatorArgs);
using ptr_ActivationDesc = MIOPEN_MANAGE_PTR(miopenActivationDescriptor_t,
                                             miopenDestroyActivationDescriptor);
ptr_FusionPlanDesc GetManagedFusionPlanDesc(miopenTensorDescriptor_t inputDesc)
{
    miopenFusionPlanDescriptor_t fusePlanDesc;
    miopenCreateFusionPlan(&fusePlanDesc, miopenVerticalFusion, inputDesc);
    return ptr_FusionPlanDesc{fusePlanDesc};
}

ptr_FusionPlanArgs GetManageFusionPlanArgs()
{
    miopenOperatorArgs_t fusionArgs;
    miopenCreateOperatorArgs(&fusionArgs);
    return ptr_FusionPlanArgs{fusionArgs};
}

ptr_ActivationDesc GetManagedActivDesc()
{
    miopenActivationDescriptor_t activdesc;
    miopenCreateActivationDescriptor(&activdesc);
    return ptr_ActivationDesc{activdesc};
}

template <class T>
struct verify_inference_batchnorm_activ
{
    tensor<T> input;
    miopenTensorDescriptor_t inputDesc{};
    miopenActivationDescriptor_t activDesc{};
    miopenTensorDescriptor_t biasScaleTensor{};
    tensor<T> bnscale{};
    tensor<T> bnbias{};
    tensor<T> estMean{};
    tensor<T> estVariance{};
    miopenBatchNormMode_t bnmode;
    miopenFusionPlanDescriptor_t fusionplan;
    miopenFusionOpDescriptor_t bNormOp;
    miopenFusionOpDescriptor_t activOp;
    double epsilon;

    verify_inference_batchnorm_activ(miopenFusionPlanDescriptor_t pfusionplan,
                                     tensor<T>& pinput,
                                     miopenActivationDescriptor_t pactivDesc,
                                     tensor<T>& pbnscale,
                                     tensor<T>& pbnbias,
                                     tensor<T>& pestMean,
                                     tensor<T>& pestVariance,
                                     miopenBatchNormMode_t pbnmode,
                                     miopenFusionOpDescriptor_t pbNormOp,
                                     miopenFusionOpDescriptor_t pactivOp)
    {
        input           = pinput;
        inputDesc       = &pinput.desc;
        activDesc       = pactivDesc;
        biasScaleTensor = &pbnscale.desc;
        bnscale         = pbnscale;
        bnbias          = pbnbias;
        estMean         = pestMean;
        estVariance     = pestVariance;
        bnmode          = pbnmode;
        fusionplan      = pfusionplan;
        bNormOp         = pbNormOp;
        activOp         = pactivOp;
        epsilon         = 1.0e-5;
    }

    tensor<T> cpu() const
    {
        auto bout = input;
        std::fill(bout.begin(), bout.end(), 0.);
        auto aout = input;
        std::fill(aout.begin(), aout.end(), 0.);

        double activ_alpha, activ_beta, activ_gamma;
        miopenActivationMode_t activ_mode;
        miopenGetActivationDescriptor(
            activDesc, &activ_mode, &activ_alpha, &activ_beta, &activ_gamma);

        if(bnmode == miopenBNPerActivation)
        {
            batchNormPerActivHostInference(
                input, bout, bnscale, bnbias, epsilon, estMean, estVariance);
        }
        else
        {
            batchNormSpatialHostInference(
                input, bout, bnscale, bnbias, epsilon, estMean, estVariance);
        }
        activationHostInfer(activ_mode, activ_gamma, activ_beta, activ_alpha, bout.data, aout.data);
        return aout;
    }

    tensor<T> gpu() const
    {
        auto&& handle = get_handle();
        auto baout    = input;
        std::fill(baout.begin(), baout.end(), 0.);
        auto in_dev          = handle.Write(input.data);
        auto out_dev         = handle.Write(baout.data);
        auto bnscale_dev     = handle.Write(bnscale.data);
        auto bnbias_dev      = handle.Write(bnbias.data);
        auto estMean_dev     = handle.Write(estMean.data);
        auto estVariance_dev = handle.Write(estVariance.data);

        double activ_alpha, activ_beta, activ_gamma;
        miopenActivationMode_t activ_mode;
        miopenGetActivationDescriptor(
            activDesc, &activ_mode, &activ_alpha, &activ_beta, &activ_gamma);

        double alpha = 1., beta = 0.;
        auto ptr_fusionargs = GetManageFusionPlanArgs();
        miopenSetOpArgsBatchNormInference(ptr_fusionargs.get(),
                                          bNormOp,
                                          &alpha,
                                          &beta,
                                          bnscale_dev.get(),
                                          bnbias_dev.get(),
                                          estMean_dev.get(),
                                          estVariance_dev.get(),
                                          epsilon);
        miopenSetOpArgsActivForward(
            ptr_fusionargs.get(), activOp, &alpha, &beta, activ_alpha, activ_beta, activ_gamma);
        miopenExecuteFusionPlan(&handle,
                                fusionplan,
                                inputDesc,
                                in_dev.get(),
                                inputDesc,
                                out_dev.get(),
                                ptr_fusionargs.get());
        baout.data = handle.Read<T>(out_dev, baout.data.size());
        return baout;
    }

    void fail(float = 0) const { std::cerr << "BatchNorm+Activation Inference:" << std::endl; }
};

static std::string transform_mode(std::string s)
{
    return miopen::RemovePrefix(miopen::ToUpper(s), "MIOPENACTIVATION");
}

template <class T>
struct na_fusion_driver : test_driver
{
    tensor<T> input;
    tensor<T> scale;
    tensor<T> shift;
    tensor<T> estMean;
    tensor<T> estVariance;
    ptr_ActivationDesc ptr_activdesc = nullptr;

    miopenActivationMode_t activ_mode = miopenActivationRELU;
    std::string amode;
    miopenBatchNormMode_t bnmode{};
    int batchnormMode = 0;

    unsigned long max_value = miopen_type<T>{} == miopenHalf ? 5 : 17;
    double alpha = 0., beta = 0., gamma = 0.;

    na_fusion_driver()
    {
        add(input, "input", get_input_tensor());
        add(alpha, "alpha", generate_data({/*1.,*/ 0.5}));
        add(beta, "beta", generate_data({/*0.,*/ 0.5}));
        add(gamma, "gamma", generate_data({/*1.,*/ 0.5}));
        add(amode,
            "amode",
            generate_data(
                {"MIOPENACTIVATIONRELU", "MIOPENACTIVATIONLOGISTIC", "MIOPENACTIVATIONABS"}));
        add(batchnormMode, "batch-norm-mode", generate_data({0, 1}));
    }

    void run()
    {
        amode = transform_mode(amode);

        if(amode == "PASSTHRU")
            activ_mode = miopenActivationPASTHRU;
        else if(amode == "LOGISTIC")
            activ_mode = miopenActivationLOGISTIC;
        else if(amode == "TANH")
            activ_mode = miopenActivationTANH;
        else if(amode == "RELU")
            activ_mode = miopenActivationRELU;
        else if(amode == "SOFTRELU")
            activ_mode = miopenActivationSOFTRELU;
        else if(amode == "ABS")
            activ_mode = miopenActivationABS;
        else if(amode == "POWER")
            activ_mode = miopenActivationPOWER;
        else if(amode == "CLIPPEDRELU")
            activ_mode = miopenActivationCLIPPEDRELU;
        else if(amode == "LEAKYRELU")
            activ_mode = miopenActivationLEAKYRELU;
        else if(amode == "ELU")
            activ_mode = miopenActivationELU;

        int input_c, input_h, input_w;
        std::tie(std::ignore, input_c, input_h, input_w) = miopen::tien<4>(input.desc.GetLengths());
        ptr_activdesc = GetManagedActivDesc();
        miopenSetActivationDescriptor(ptr_activdesc.get(), activ_mode, alpha, beta, gamma);

        if(batchnormMode == 1)
        {
            bnmode = miopenBNSpatial;
        }
        else if(batchnormMode == 0)
        {
            bnmode = miopenBNPerActivation;
        }

        std::size_t ssn, ssc, ssh, ssw;
        auto derivedBnDesc = miopen::TensorDescriptor{};
        miopen::DeriveBNTensorDescriptor(derivedBnDesc, input.desc, bnmode);
        std::tie(ssn, ssc, ssh, ssw) = miopen::tien<4>(derivedBnDesc.GetLengths());

        if(input.desc.GetType() == miopenFloat)
        {
            scale       = tensor<T>{ssn, ssc, ssh, ssw}.generate(rand_gen{});
            shift       = tensor<T>{ssn, ssc, ssh, ssw}.generate(rand_gen{});
            estMean     = tensor<T>{ssn, ssc, ssh, ssw}.generate(rand_gen{});
            estVariance = tensor<T>{ssn, ssc, ssh, ssw}.generate(rand_gen{});
        }
        else
        {
            scale       = tensor<T>{ssn, ssc, ssh, ssw};
            shift       = tensor<T>{ssn, ssc, ssh, ssw};
            estMean     = tensor<T>{ssn, ssc, ssh, ssw};
            estVariance = tensor<T>{ssn, ssc, ssh, ssw};

            srand(0);
            for(int i = 0; i < scale.desc.GetElementSize(); i++)
            {
                scale[i]       = (((rand() % 2) == 1) ? -1 : 1) * 1e-4 * T(rand() % 100);
                shift[i]       = (((rand() % 2) == 1) ? -1 : 1) * 1e-4 * T(rand() % 100);
                estMean[i]     = (((rand() % 2) == 1) ? -1 : 1) * 1e-4 * T(rand() % 100);
                estVariance[i] = std::fabs((((rand() % 2) == 1) ? -1 : 1) * 1e-2 * T(rand() % 100));
            }
            for(int i = 0; i < input.desc.GetElementSize(); i++)
            {
                input[i] = (((rand() % 2) == 1) ? -1 : 1) * T(rand() % 100);
            }
        }

        auto&& handle = get_handle();

        miopenFusionOpDescriptor_t bNormOp = nullptr;
        miopenFusionOpDescriptor_t activOp = nullptr;

        auto ptr_fusionplan = GetManagedFusionPlanDesc(&input.desc);

        miopenCreateOpBatchNormInference(ptr_fusionplan.get(), &bNormOp, bnmode, &scale.desc);
        miopenCreateOpActivationForward(ptr_fusionplan.get(), &activOp, activ_mode);

        miopenStatus_t miopenError = miopenCompileFusionPlan(&handle, ptr_fusionplan.get());
        if(miopenError != miopenStatusSuccess)
        {
            std::cerr << "BatchNorm+Activation Inference plan not supported." << std::endl;
        }
        else
        {
            verify(verify_inference_batchnorm_activ<T>{ptr_fusionplan.get(),
                                                       input,
                                                       ptr_activdesc.get(),
                                                       scale,
                                                       shift,
                                                       estMean,
                                                       estVariance,
                                                       bnmode,
                                                       bNormOp,
                                                       activOp});
        }
    }
};

int main(int argc, const char* argv[]) { test_drive<na_fusion_driver>(argc, argv); }
