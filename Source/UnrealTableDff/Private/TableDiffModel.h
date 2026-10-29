#pragma once

#include "TableDiffTypes.h"

class UDataTable;

class FTableDiffModel
{
public:
	static FTableDiffResult Build(const UDataTable* OldTable, const UDataTable* NewTable);
};
