#include "CustomBPNodes/DownloadImageUsingBaseAsyncTask.h"

#include "CustomBPNodes/DownloadImageProxy.h"

#define LOCTEXT_NAMESPACE "UK2Node_AsyncDownloadImage"

UK2Node_AsyncDownloadImage::UK2Node_AsyncDownloadImage()
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UAsyncDownloadImageProxy, CreateAsyncDownloadImageProxy);
	ProxyFactoryClass = UAsyncDownloadImageProxy::StaticClass();
	ProxyClass = UAsyncDownloadImageProxy::StaticClass();
}

FText UK2Node_AsyncDownloadImage::GetTooltipText() const
{
	return LOCTEXT("K2Node_AsyncDownloadImage_Tooltip", "Asynchronously download an image resource from given URL");
}

FText UK2Node_AsyncDownloadImage::GetNodeTitle(ENodeTitleType::Type InTitleType) const
{
	return LOCTEXT("AsyncDownloadImage", "Async Download Image");
}

FText UK2Node_AsyncDownloadImage::GetMenuCategory() const
{
	return LOCTEXT("ThreadingSampleCategory", "ThreadingSample|AsyncDownloadImage");
}

#undef LOCTEXT_NAMESPACE