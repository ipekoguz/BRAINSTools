/*=========================================================================
 *
 *  Program:   Insight Segmentation & Registration Toolkit
 *  Module:    $RCSfile$
 *  Language:  C++
 *
 *  Copyright (c) Insight Software Consortium. All rights reserved.
 *  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.
 *
 *  This software is distributed WITHOUT ANY WARRANTY; without even
 *  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the above copyright notices for more information.
 *
 *  =========================================================================*/
#ifndef __genericRegistrationHelper_h
#define __genericRegistrationHelper_h

#include "BRAINSCommonLib.h"

#if (ITK_VERSION_MAJOR < 4)
#include "itkOptImageToImageMetric.h"
#include "itkOptMattesMutualInformationImageToImageMetric.h"
#else
#include "itkImageToImageMetric.h"
#include "itkMattesMutualInformationImageToImageMetric.h"
#endif
#define COMMON_MMI_METRIC_TYPE itk::MattesMutualInformationImageToImageMetric

#include "itkImageRegistrationMethod.h"
#include "itkLinearInterpolateImageFunction.h"
#include "itkImage.h"
#include "itkDataObjectDecorator.h"

#include "itkCenteredTransformInitializer.h"

#include "itkRescaleIntensityImageFilter.h"
#include "itkShiftScaleImageFilter.h"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>

#include "itkMultiThreader.h"
#include "itkResampleImageFilter.h"

#ifdef USE_DebugImageViewer
#include "DebugImageViewerClient.h"
#include "itkLinearInterpolateImageFunction.h"
#include "Imgmath.h"
#endif

#include "itkIO.h"

#include <cstdio>

enum
  {
  Dimension = 3,
  MaxInputDimension = 4
  };

//  The following section of code implements a Command observer
//  that will monitor the evolution of the registration process.
//
#include "itkCommand.h"

namespace BRAINSFit
{
template <class JointPDFType>
void MakeDebugJointHistogram(const std::string & debugOutputDirectory, const typename JointPDFType::Pointer myHistogram,
                             const int globalIteration,
                             const int currentIteration)
{
  std::stringstream fn("");

  fn << debugOutputDirectory << "/DEBUGHistogram_"
     << std::setw( 4 ) << std::setfill( '0' ) << globalIteration
     << "_"
     << std::setw( 4 ) << std::setfill( '0' ) << currentIteration
     << ".png";

  itk::ImageRegionConstIterator<JointPDFType> origIter(myHistogram, myHistogram->GetLargestPossibleRegion() );
  origIter.GoToBegin();
  unsigned int nonZeroCount   = 0;
  float        nonZeroAverage = 0;

  while( !origIter.IsAtEnd() )
    {
    const float currValue = origIter.Get();
    if( currValue > 0 )
      {
      nonZeroAverage += currValue;
      ++nonZeroCount;
      }
    ++origIter;
    }

  nonZeroAverage /= static_cast<float>(nonZeroCount);

  typedef itk::Image<unsigned short, 2> PNGImageType;
  typename PNGImageType::Pointer myOut = PNGImageType::New();
  myOut->CopyInformation(myHistogram);
  myOut->SetRegions(myHistogram->GetLargestPossibleRegion() );
  myOut->Allocate();
  myOut->FillBuffer(0U);
  itk::ImageRegionIterator<PNGImageType> pngIter(myOut, myOut->GetLargestPossibleRegion() );
  pngIter.GoToBegin();
  origIter.GoToBegin();
  while( !pngIter.IsAtEnd() )
    {
    const float MAX_VALUE = vcl_numeric_limits<unsigned short>::max();
    const float scaleFactor = 0.66 * MAX_VALUE / nonZeroAverage;
    const float currValue = (origIter.Get() ) * scaleFactor;
    if( currValue < 0 )
      {
      pngIter.Set( 0 );
      }
    else if( currValue > MAX_VALUE )
      {
      pngIter.Set( MAX_VALUE );
      }
    else
      {
      pngIter.Set( currValue );
      }
    ++pngIter;
    ++origIter;
    }

  itkUtil::WriteImage<PNGImageType>( myOut, fn.str() );
  std::cout << "Writing jointPDF: " << fn.str() << std::endl;
}

template <typename TOptimizer, typename TTransform, typename TImage>
class CommandIterationUpdate : public itk::Command
{
public:
  typedef  CommandIterationUpdate Self;
  typedef  itk::Command           Superclass;
  typedef itk::SmartPointer<Self> Pointer;
  itkNewMacro(Self);
  typedef          TOptimizer  OptimizerType;
  typedef const OptimizerType *OptimizerPointer;

  typedef typename COMMON_MMI_METRIC_TYPE<TImage, TImage> MattesMutualInformationMetricType;
  void SetDisplayDeformedImage(bool x)
  {
    m_DisplayDeformedImage = x;
#ifdef USE_DebugImageViewer
    m_DebugImageDisplaySender.SetEnabled(x);
#endif
  }

  void SetPromptUserAfterDisplay(bool x)
  {
    m_PromptUserAfterDisplay = x;
#ifdef USE_DebugImageViewer
    m_DebugImageDisplaySender.SetPromptUser(x);
#endif
  }

  void SetPrintParameters(bool x)
  {
    m_PrintParameters = x;
  }

  void SetFixedImage(typename TImage::Pointer fixed)
  {
    m_FixedImage = fixed;
  }

  void SetMovingImage(typename TImage::Pointer moving)
  {
    m_MovingImage = moving;
  }

  void SetTransform(typename TTransform::Pointer & xfrm)
  {
    m_Transform = xfrm;
  }

  void Execute(itk::Object *caller, const itk::EventObject & event)
  {
    Execute( (const itk::Object *)caller, event );
  }

  typename TImage::Pointer Transform(typename TTransform::Pointer & xfrm)
  {
    //      std::cerr << "Moving Volume (in observer): " << this->m_MovingImage
    // << std::endl;
    typedef typename itk::LinearInterpolateImageFunction<TImage, double> InterpolatorType;
    typename InterpolatorType::Pointer interp = InterpolatorType::New();
    typedef typename itk::ResampleImageFilter<TImage, TImage> ResampleImageFilter;
    typename ResampleImageFilter::Pointer resample = ResampleImageFilter::New();
    resample->SetInput(m_MovingImage);
    resample->SetTransform(xfrm);
    resample->SetInterpolator(interp);
    resample->SetOutputParametersFromImage(m_FixedImage);
    resample->SetOutputDirection( m_FixedImage->GetDirection() );
    resample->SetDefaultPixelValue(0);
    resample->Update();
    typename TImage::Pointer rval = resample->GetOutput();
    // rval->DisconnectPipeline();
    return rval;
  }

  void Execute(const itk::Object *object, const itk::EventObject & event)
  {
    OptimizerPointer optimizer = dynamic_cast<OptimizerPointer>( object );

    if( optimizer == NULL )
      {
      itkGenericExceptionMacro("fail to convert to Optimizer Pointer");
      }
    if( !itk::IterationEvent().CheckEvent(&event) )
      {
      return;
      }

    typename OptimizerType::ParametersType parms =
      optimizer->GetCurrentPosition();
    int  psize = parms.GetNumberOfElements();
    bool parmsNonEmpty = false;
    for( int i = 0; i < psize; ++i )
      {
      if( parms[i] != 0.0 )
        {
        parmsNonEmpty = true;
        break;
        }
      }

    if( m_PrintParameters )
      {
      std::cout << std::setw(  4 ) << std::setfill( ' ' ) << optimizer->GetCurrentIteration() << "   ";
      std::cout << std::setw( 10 ) << std::setfill( ' ' ) << optimizer->GetValue() << "   ";
      if( parmsNonEmpty )
        {
        std::cout << parms;
        }
      std::cout << std::endl;
      }
#if ITK_VERSION_MAJOR >= 4  // GetJointPDF only available in ITKv4
    //
    // GenerateHistogram
    // TODO: KENT:  BRAINSFit tools need to define a common output directory for
    // all debug images to be written.
    //             by default the it should be the same as the outputImage, and
    // if that does not exists, then it
    //             should default to the same directory as the outputTransform,
    // or it should be specified by the
    //             user on the command line.
    //             The following function should only be called when BRAINSFit
    // command line tool is called with
    //             --debugLevel 7 or greater, and it should write the 3D
    // JointPDF image to the debugOutputDirectory
    //             location.
    std::string debugOutputDirectory("");
    if( optimizer->GetCurrentIteration() < 5 // Only do first 4 of each iteration
        &&
        itksys::SystemTools::GetEnv("DEBUG_JOINTHISTOGRAM_DIR", debugOutputDirectory) && debugOutputDirectory != "" )
      {
      // Special BUG work around for MMI metric
      // that does not work in multi-threaded mode
      // typedef COMMON_MMI_METRIC_TYPE<TImage,TImage> MattesMutualInformationMetricType;
      static int TransformIterationCounter = 0;
      typename MattesMutualInformationMetricType::Pointer test_MMICostMetric =
        dynamic_cast<MattesMutualInformationMetricType *>(this->m_CostMetricObject.GetPointer() );
      if( test_MMICostMetric.IsNotNull() )
        {
        typedef typename MattesMutualInformationMetricType::PDFValueType PDFValueType;
        typedef itk::Image<PDFValueType, 2>                              JointPDFType;
        const typename JointPDFType::Pointer myHistogram = test_MMICostMetric->GetJointPDF();
        MakeDebugJointHistogram<JointPDFType>(debugOutputDirectory, myHistogram, TransformIterationCounter,
                                              optimizer->GetCurrentIteration() );
        if( optimizer->GetCurrentIteration()  == 0 )
          {
          TransformIterationCounter += 10000;
          }
        }
      }
#endif

#ifdef USE_DebugImageViewer
    if( m_DisplayDeformedImage )
      {
      if( parmsNonEmpty )
        {
        m_Transform->SetParametersByValue(parms);
        }
      // else, if it is a vnl optimizer wrapper, i.e., the BSpline optimizer,
      // the only hint you get
      // is in the transform object used by the optimizer, so don't erase it,
      // use it.
      typename TImage::Pointer transformResult =
        this->Transform(m_Transform);
      //      std::cerr << "Moving Volume (after transform): " <<
      // transformResult << std::endl;
      m_DebugImageDisplaySender.SendImage<TImage>(transformResult, 1);
      typename TImage::Pointer diff = Isub<TImage>(m_FixedImage, transformResult);

      m_DebugImageDisplaySender.SendImage<TImage>(diff, 2);
      }
#endif
  }

  void SetMetricObject(typename MattesMutualInformationMetricType::Pointer metric_Object)
  {
    // NOTE:  Returns NULL if not MattesMutualInformationImageToImageMetric
    this->m_CostMetricObject = dynamic_cast<MattesMutualInformationMetricType *>(metric_Object.GetPointer() );
    // NO NEED FOR CHECKING IF DYNAMIC CAST WORKED, invalid cast is OK with a
    // NULL return
  }

protected:
  CommandIterationUpdate() : m_DisplayDeformedImage(false),
    m_PromptUserAfterDisplay(false),
    m_PrintParameters(true),
    m_MovingImage(NULL),
    m_FixedImage(NULL),
    m_Transform(NULL),
    m_CostMetricObject(NULL)
  {
  }

private:
  bool m_DisplayDeformedImage;
  bool m_PromptUserAfterDisplay;
  bool m_PrintParameters;

  typename TImage::Pointer m_MovingImage;
  typename TImage::Pointer m_FixedImage;
  typename TTransform::Pointer m_Transform;

  typename MattesMutualInformationMetricType::Pointer m_CostMetricObject;

#ifdef USE_DebugImageViewer
  DebugImageViewerClient m_DebugImageDisplaySender;
#endif
};
} // end namespace BRAINSFit

namespace itk
{
// TODO:  Remove default MetricType here, and force a choice
template <typename TTransformType, typename TOptimizer, typename TFixedImage,
          typename TMovingImage, typename MetricType>
class ITK_EXPORT MultiModal3DMutualRegistrationHelper : public ProcessObject
{
public:
  /** Standard class typedefs. */
  typedef MultiModal3DMutualRegistrationHelper Self;
  typedef ProcessObject                        Superclass;
  typedef SmartPointer<Self>                   Pointer;
  typedef SmartPointer<const Self>             ConstPointer;

  /** Method for creation through the object factory. */
  itkNewMacro(Self);

  /** Run-time type information (and related methods). */
  itkTypeMacro(MultiModal3DMutualRegistrationHelper, ProcessObject);

  typedef          TFixedImage                  FixedImageType;
  typedef typename FixedImageType::ConstPointer FixedImageConstPointer;
  typedef typename FixedImageType::Pointer      FixedImagePointer;

  typedef          TMovingImage                  MovingImageType;
  typedef typename MovingImageType::ConstPointer MovingImageConstPointer;
  typedef typename MovingImageType::Pointer      MovingImagePointer;

  typedef          TTransformType         TransformType;
  typedef typename TransformType::Pointer TransformPointer;

  /** Type for the output: Using Decorator pattern for enabling
    *  the Transform to be passed in the data pipeline */
  typedef DataObjectDecorator<TransformType>    TransformOutputType;
  typedef typename TransformOutputType::Pointer TransformOutputPointer;
  typedef typename TransformOutputType::ConstPointer
    TransformOutputConstPointer;

  typedef          TOptimizer                    OptimizerType;
  typedef const OptimizerType *                  OptimizerPointer;
  typedef typename OptimizerType::ScalesType     OptimizerScalesType;
  typedef typename OptimizerType::ParametersType OptimizerParametersType;

  typedef typename MetricType::FixedImageMaskType  FixedBinaryVolumeType;
  typedef typename FixedBinaryVolumeType::Pointer  FixedBinaryVolumePointer;
  typedef typename MetricType::MovingImageMaskType MovingBinaryVolumeType;
  typedef typename MovingBinaryVolumeType::Pointer MovingBinaryVolumePointer;

  typedef LinearInterpolateImageFunction<
      MovingImageType,
      double>    InterpolatorType;

  typedef ImageRegistrationMethod<FixedImageType, MovingImageType> RegistrationType;
  typedef typename RegistrationType::Pointer                       RegistrationPointer;

  typedef itk::CenteredTransformInitializer<
      TransformType,
      FixedImageType,
      MovingImageType>    TransformInitializerType;

  typedef itk::ResampleImageFilter<
      MovingImageType,
      FixedImageType>     ResampleFilterType;

  /** Initialize by setting the interconnects between the components. */
  virtual void Initialize(void); // throw ( ExceptionObject );

  /** Method that initiates the registration. */
  void Update(void);

  /** Set/Get the Fixed image. */
  void SetFixedImage(FixedImagePointer fixedImage);

  itkGetConstObjectMacro(FixedImage, FixedImageType);

  /** Set/Get the Moving image. */
  void SetMovingImage(MovingImagePointer movingImage);

  itkGetConstObjectMacro(MovingImage, MovingImageType);

  /** Set/Get the InitialTransfrom. */
  void SetInitialTransform(typename TransformType::Pointer initialTransform);
  itkGetConstObjectMacro(InitialTransform, TransformType);

  /** Set/Get the Transfrom. */
  itkSetObjectMacro(Transform, TransformType);
  typename TransformType::Pointer GetTransform(void);

  // itkSetMacro( PermitParameterVariation, std::vector<int>      );

  itkSetObjectMacro(CostMetricObject, MetricType);
  itkGetConstObjectMacro(CostMetricObject, MetricType);

  itkSetMacro(NumberOfSamples,               unsigned int);
  itkSetMacro(NumberOfHistogramBins,         unsigned int);
  itkSetMacro(NumberOfIterations,            unsigned int);
  itkSetMacro(RelaxationFactor,              double);
  itkSetMacro(MaximumStepLength,             double);
  itkSetMacro(MinimumStepLength,             double);
  itkSetMacro(TranslationScale,              double);
  itkSetMacro(ReproportionScale,             double);
  itkSetMacro(SkewScale,                     double);
  itkSetMacro(InitialTransformPassThruFlag,  bool);
  itkSetMacro(BackgroundFillValue,           double);
  itkSetMacro(DisplayDeformedImage,          bool);
  itkSetMacro(PromptUserAfterDisplay,        bool);
  itkGetConstMacro(FinalMetricValue,         double);
  itkGetConstMacro(ActualNumberOfIterations, unsigned int);
  itkSetMacro(ObserveIterations,        bool);
  itkGetConstMacro(ObserveIterations,        bool);
  // Debug option for MI metric
  itkSetMacro(ForceMINumberOfThreads, int);
  itkGetConstMacro(ForceMINumberOfThreads, int);

  /** Returns the transform resulting from the registration process  */
  const TransformOutputType * GetOutput() const;

  /** Make a DataObject of the correct type to be used as the specified
    * output. */
#if (ITK_VERSION_MAJOR < 4)
  // Nothing to add for ITKv3
#else
  using Superclass::MakeOutput;
#endif
  virtual DataObjectPointer MakeOutput(unsigned int idx);

  /** Method to return the latest modified time of this object or
    * any of its cached ivars */
  unsigned long GetMTime() const;

  /** Method to set the Permission to vary by level  */
  void SetPermitParameterVariation(std::vector<int> perms)
  {
    m_PermitParameterVariation.resize( perms.size() );
    for( unsigned int i = 0; i < perms.size(); ++i )
      {
      m_PermitParameterVariation[i] = perms[i];
      }
  }

protected:
  MultiModal3DMutualRegistrationHelper();
  virtual ~MultiModal3DMutualRegistrationHelper()
  {
  }

  void PrintSelf(std::ostream & os, Indent indent) const;

  /** Method invoked by the pipeline in order to trigger the computation of
    * the registration. */
  void  GenerateData();

private:
  MultiModal3DMutualRegistrationHelper(const Self &);             // purposely
                                                                  // not
  // implemented
  void operator=(const Self &);                                   // purposely

  // not

  // implemented

  // FixedImageConstPointer           m_FixedImage;
  // MovingImageConstPointer          m_MovingImage;

  FixedImagePointer  m_FixedImage;
  MovingImagePointer m_MovingImage;
  TransformPointer   m_InitialTransform;
  TransformPointer   m_Transform;
  //
  // make sure parameters persist until after they're used by the transform

  RegistrationPointer m_Registration;

  std::vector<int> m_PermitParameterVariation;
  typename MetricType::Pointer  m_CostMetricObject;

  unsigned int m_NumberOfSamples;
  unsigned int m_NumberOfHistogramBins;
  unsigned int m_NumberOfIterations;
  double       m_RelaxationFactor;
  double       m_MaximumStepLength;
  double       m_MinimumStepLength;
  double       m_TranslationScale;
  double       m_ReproportionScale;
  double       m_SkewScale;
  bool         m_InitialTransformPassThruFlag;
  double       m_BackgroundFillValue;
  unsigned int m_ActualNumberOfIterations;
  bool         m_DisplayDeformedImage;
  bool         m_PromptUserAfterDisplay;
  double       m_FinalMetricValue;
  bool         m_ObserveIterations;
  // DEBUG OPTION:
  int m_ForceMINumberOfThreads;

  ModifiedTimeType m_InternalTransformTime;
};
} // end namespace itk

#ifndef ITK_MANUAL_INSTANTIATION
#include "genericRegistrationHelper.hxx"
#endif

#endif
