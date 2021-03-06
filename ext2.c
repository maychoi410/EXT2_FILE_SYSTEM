typedef struct
{
	char *address;
} DISK_MEMORY;

#include "ext2.h"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// 파일 쓰기
int ext2_write(EXT2_NODE *file, unsigned long offset, unsigned long length, const char *buffer)
{
	BYTE sector[MAX_SECTOR_SIZE];					 //섹터크기만큼 배열 크기 설정
	DWORD currentOffset, currentBlock, blockSeq = 0; //
	DWORD blockNumber, sectorNumber, sectorOffset;	 //
	DWORD readEnd;
	DWORD blockSize;
	INODE node;
	int i;

	get_inode(file->fs, file->entry.inode, &node); // 작성할 파일의 아이노드 메타데이터를 node에 저장
	currentBlock = node.block[0];				   // 기록할 블록 번호
	readEnd = offset + length;					   // 쓰고자 하는 마지막 위치를 계산

	currentOffset = offset; // 기록할 offset

	blockSize = MAX_BLOCK_SIZE;

	i = 1;
	while (offset > blockSize) //기록할 위치 찾는 루프
	{
		currentBlock = get_data_block_at_inode(file->fs, node, ++i); // node의 i번째 데이터블록 번호
		blockSize += blockSize;										 //암만봐도 NEXT_BLOCK_SIZE 넣는것 같다. TODO
		blockSeq++;
	}

	while (currentOffset < readEnd) // 현재 offset이 쓰고자 하는 마지막 위치보다 앞쪽인동안
	{
		DWORD copyLength;

		blockNumber = currentOffset / MAX_BLOCK_SIZE; // 몇 번째 블록인지 계산

		if (currentBlock == 0) // currentBlock이 할당되지 않은 경우
		{
			if (expand_block(file->fs, file->entry.inode) == EXT2_ERROR) //file->entry.inode 에 해당하는 아이노드를 찾아서 해당 아이노드의 비어있는 블록에 새로운 데이터 블록 할당.
				return EXT2_ERROR;
			process_meta_data_for_block_used(file->fs, file->entry.inode); //프로세스가 처리하다의 프로세스 같다. expand_block 에서 데이터 블록을 할당이나 해제 했을때, free_block_count나 block_bitmap같은 메타 데이터 수정
			get_inode(file->fs, file->entry.inode, &node);				   // file->entry.inode의 메타데이터를 node에 write
			currentBlock = node.block[0];								   // currentBlock을 할당된 데이터블록 번호로 지정
		}

		if (blockSeq != blockNumber) //다음 데이터 블록으로 넘어감
		{
			DWORD nextBlock;
			blockSeq++; // 몇번째 블록까지 읽었는지 저장하는 변수도 증가
			++i;
			nextBlock = get_data_block_at_inode(file->fs, node, i); // node의 i번째 데이터블록 번호를 리턴
			if (nextBlock == 0)										// nextBlock이 할당되지 않은 경우
			{
				expand_block(file->fs, file->entry.inode);					   // 데이터블록 할당
				get_inode(file->fs, file->entry.inode, &node);				   // file->entry.inode의 메타데이터를 node에 write
				process_meta_data_for_block_used(file->fs, file->entry.inode); // ?

				nextBlock = get_data_block_at_inode(file->fs, node, i); // node의 i번째 데이터블록 번호를 리턴

				if (nextBlock == 0) // nextBlock이 또 할당되지 않은 경우
				{
					return EXT2_ERROR;
				}
			}
			currentBlock = nextBlock; // 다음 블록으로 이동
		}
		sectorNumber = (currentOffset / (MAX_SECTOR_SIZE)) % (MAX_BLOCK_SIZE / MAX_SECTOR_SIZE); // 몇 번째 섹터인지 계산
		sectorOffset = currentOffset % MAX_SECTOR_SIZE;											 // 섹터 내의 위치 계산

		copyLength = MIN(MAX_SECTOR_SIZE - sectorOffset, readEnd - currentOffset); // 섹터에서 읽어온 바이트 수와 더 읽어야 할 바이트 수 중 작은 값을 버퍼로 복사할 크기로 정함

		if (copyLength != MAX_SECTOR_SIZE) // 복사할 크기가 섹터 하나의 크기와 같이 않으면
										   // -> 한 섹터의 내용을 모두 바꿀거면 굳이 읽어올 필요가 없지만
										   //    일부만 바꿀 경우 섹터 단위로 읽어오지 않고 데이터를 쓸 경우 나머지 데이터가 원하지 않는 값으로 변경될 수 있음
		{
			if (data_read(file->fs, file->fs->sb.block_group_number, currentBlock, sector)) //두번째 인자가 0일때는 그룹이 하나 일때, 여러개면 지금 있는게 맞다.
				break;
		}

		memcpy(&sector[sectorOffset], buffer, copyLength); // 버퍼에서 새로 쓸 데이터 복사

		if (data_write(file->fs, 0, currentBlock, sector))
			break;

		buffer += copyLength;
		currentOffset += copyLength;
	}

	node.size = MAX(currentOffset, node.size); // 새로 쓴 영역과 기존 파일의 크기 중 큰 값을 파일 크기로 설정
	set_inode_onto_inode_table(file->fs, file->entry.inode, &node); // 아이노드 테이블 업데이트

	return currentOffset - offset;
}

UINT32 get_free_inode_number(EXT2_FILESYSTEM *fs);

void process_meta_data_for_inode_used(EXT2_NODE *retEntry, UINT32 inode_num, int fileType)
{
	/*
	아이노드를 할당, 해제했을 때 free_inode_count, inode_bitmap 등의 메타데이터를 알맞게 수정하는 함수
 	마찬가지로 아이노드를 수정한 후 이 함수를 호출할 경우 이 함수에서 수정된 아이노드의 번호를 가지고 수정내역을 다시 탐색 후 
	메타데이터를 수정해야 할 것 같은데 그럴 바에는 애초에 아이노드 할당이나 해제하는 함수에서 이 작업도 같이하는 편이 나을 듯
	세 번째 인자일 fileType은 파일이 디렉토리인지 아닌지 등을 판단해 메타데이터 수정 시 사용할 듯
	*/
}

// 디스크의 location에 value를 기록 (eunseo)
int set_entry( EXT2_FILESYSTEM* fs, const EXT2_DIR_ENTRY_LOCATION* location, const EXT2_DIR_ENTRY* value)
{
	/* value의 크기가 섹터만큼 크지 않더라도 바로 기록하면 나머지 정보들이 원하지 않는 값으로 바뀔 수 있기 때문에
	섹터 단위로 읽어와 해당 영역만 바꿔준 후 다시 기록함 */
	BYTE	sector[MAX_SECTOR_SIZE];
	EXT2_DIR_ENTRY*	entry;

	data_read(fs, location->group, location->block, sector); // 디스크에서 해당 위치의 정보를 sector 버퍼로 읽어옴

	entry = (EXT2_DIR_ENTRY*)sector; // 섹터의 시작주소
	entry[location->offset] = *value; // 원하는 값을 기록

	data_write(fs, location->group, location->block, sector); // 디스크에 sector 버퍼의 정보를 씀

	return EXT2_ERROR;
}

// inode_num의 데이터 블록에 새로운 엔트리(retEntry) 추가. 성공여부 return (eunseo)
int insert_entry(UINT32 inode_num, EXT2_NODE *retEntry, int fileType)
{
	/*
	인자로 받은 parent의 inode_num을 가지고 해당 아이노드를 읽어온 후 새로운 엔트리를 삽입해주는 함수
 	fat에서와 같이 루트 디렉토리, overwrite 등을 생각해 주어야 할 듯(빈 엔트리 찾지 못할 경우,
	 DIR_ENTRY_NO_MORE을 나타내는 디렉토리 엔트리 추가, 위치정보 업데이트 등)
	seungmin */
	// fileType이 overwrite인 경우는 언제 필요한지 모르겠음. 일단 고려하지 않음.

	EXT2_NODE	entryNoMore;			// retEntry 다음 위치에 마지막 엔트리임을 나타내기 위한 노드
	INODE* 		inodeBuffer;			// retEntry의 새로 할당 받은 아이노드에 대한 메타데이터
	BYTE		entryName[2] = { 0, };	// 엔트리 이름을 저장하는 버퍼
	UINT32		retEntry_inodeNum;		// retEntry의 inode number가 없을 경우 새로 할당 받은 inode number
	DWORD		dataBlockNum;			// 새로 할당 받은 데이터 블록 넘버

	if ( GET_INODE_FROM_NODE(retEntry) == 0 ) // retEntry의 inode number가 없으면
	{
		retEntry_inodeNum = get_free_inode_number(retEntry->fs); // 새로운 inode number 할당
		retEntry->entry.inode = retEntry_inodeNum;

		inodeBuffer = (INODE*)malloc(sizeof(INODE));
		ZeroMemory(inodeBuffer, sizeof(INODE));
		set_inode_onto_inode_table(retEntry->fs, retEntry->entry.inode, inodeBuffer); // 아이노드 테이블 업데이트

		inodeBuffer->mode &= 0x0FFF;
		inodeBuffer->mode |= fileType; // file type 지정
	}
	else // retEntry의 inode number가 있으면
	{
		get_inode(retEntry->fs, retEntry->entry.inode, inodeBuffer); // retEntry의 아이노드 메타데이터를 inodeBuffer에 저장
		inodeBuffer->mode &= 0x0FFF;
		inodeBuffer->mode |= fileType; // file type 지정
	}

	ZeroMemory( &entryNoMore, sizeof( EXT2_NODE ) ); // entryNoMore를 0으로 초기화
	entryName[0] = DIR_ENTRY_FREE;

	// 빈 엔트리를 찾아 entryNoMore에 담아옴
	if( lookup_entry( retEntry->fs, inode_num, entryName, &entryNoMore ) == EXT2_SUCCESS ) // 빈 엔트리가 있다면
	{
		set_entry(retEntry->fs, &entryNoMore.location, &retEntry->entry); // 이 위치에 새 엔트리 기록
		retEntry->location = entryNoMore.location; // 위치정보 기록
	}
	else // 빈 엔트리가 없다면
	{
		// 마지막 엔트리 뒤에 새 엔트리 추가
		entryName[0] = DIR_ENTRY_NO_MORE;
		if (lookup_entry(retEntry->fs, inode_num, entryName, &entryNoMore) == EXT2_ERROR) // 마지막 엔트리를 찾지 못하면
			return EXT2_ERROR;


		set_entry(retEntry->fs, &entryNoMore.location, &retEntry->entry); // 마지막 엔트리를 찾았다면 이 위치에 새 엔트리 기록
		retEntry->location = entryNoMore.location; // 위치정보 기록
		entryNoMore.location.offset++;			   // 마지막 엔트리라고 저장할 위치로 이동

		// 블록에 모든 엔트리가 찼다면
		if ( entryNoMore.location.offset == MAX_BLOCK_SIZE / sizeof( EXT2_DIR_ENTRY ) )
		{
			if (inode_num == 2) // 루트 디렉터리이면
			{
				entryNoMore.location.offset--; // 기존 마지막 엔트리의 위치로 다시 이동
				entryNoMore.entry.name[0] = DIR_ENTRY_NO_MORE; // 마지막 엔트리임을 설정하고
				set_entry(retEntry->fs, &entryNoMore.location, &entryNoMore.entry); // 디스크에 저장

				WARNING( "Cannot insert entry into the root entry\n" );
				return EXT2_ERROR;
			}

			entryNoMore.location.block++; // 그 다음 블록에 저장
			entryNoMore.location.offset = 0;

			if (expand_block(retEntry->fs, inode_num) == EXT2_ERROR) // 새로운 데이터 블록 할당
				return EXT2_ERROR;
			// process_meta_data_for_block_used(retEntry->fs, file->entry.inode); // ?
		}
	
		set_entry(retEntry->fs, &entryNoMore.location, &entryNoMore.entry); // 마지막이라고 기록
	}

	return EXT2_SUCCESS;
}

//fs와 아이노드 번호가 들어오면, 아이노드 번호로 블록 그룹을 계산. 계산된 블록 그룹에서 먼저 블록 할당을 하려고함, 블록 그룹에 더이상 할당 가능한 데이터 블럭이 없으면, 인근 블록 그룹에서 블록 가져오는 것으로 하겠음.
UINT32 get_available_data_block(EXT2_FILESYSTEM *fs, UINT32 inode_num)
{
	/*
	사실 선언만 되어 있고 사용되는 곳이 없어서 함수의 정확한 기능을 유추하기는 불가능. 따라서 기능은 정의하기 나름일 것 같음
	함수가 사용되고 있지 않음 -> 정의되지 않은 함수 내에서 사용될 것으로 생각됨 – expand_block 함수에서 사용 될 것으로 예상
 	함수명과 인자를 바탕으로 생각해봤을 때 첫 번째 인자로 받은 EXT2_FILESYSTEM 구조체에서 free_block_count를 확인해 할당할 블록이 있는지 확인하고, 
	있다면 block_bitmap에서 빈 블록 번호를 return
 	expand_block 함수에서는 return 받은 블록 번호를 아이노드의 block 필드의 비어 있는 인덱스에 연결하고, process_meta_data_for_block_used를 통해 메타데이터 수정
	*/


	UINT32 result, inode_which_block_group;	//result : 사용가능한 블록 번호를 저장할 변수, inode_which_block_group : 인자로 받은 아이노드가 어느 블록 그룹에 있는지
	UINT32 sector_num_per_block = MAX_BLOCK_SIZE / MAX_SECTOR_SIZE;	//블럭당 섹터의 개수
	UINT32 k = 0;	//그룹 번호 저장
	BYTE sector[MAX_SECTOR_SIZE];	//블럭 비트맵을 가져와서 저장할 공간.
	const SECTOR BOOT_BLOCK = 1;	//부트 섹터를 제외한 파일시스템의 기본번지 설정번지에 위치하도록
	EXT2_FILESYSTEM* _fs = fs;
	EXT2_GROUP_DESCRIPTOR* gdp;	//group descriptor pointer라는 뜻

	gdp = (EXT2_GROUP_DESCRIPTOR*)_fs->gd; // error - cannot convert to a pointer type

	if (_fs->sb.free_block_count) //슈퍼블록에서 전체 데이터 블럭에서 빈공간을 탐색, 없으면, 에러 리턴, 있으면 진행.
	{
		inode_which_block_group = GET_INODE_GROUP(inode_num); //아이노드가 속해있는 블럭 그룹 계산
		if (gdp[inode_which_block_group].free_blocks_count)	  //아이노드가 속해있는 블럭 그룹에 할당가능한 데이터 블럭이 있는지 확인.
		{
			//아이노드가 있는 블럭 그룹에 할당가능한 데이터블럭이 존재하는 경우. 블럭 비트맵을 참고해서 데이터 블록 받아서 리턴.
			for (UINT32 i = 0; i < sector_num_per_block; i++) //블럭당 섹터 수 만큼 섹터를 읽는다. i: 현재 처리중인 섹터 번호.
			{
				ZeroMemory(sector, MAX_SECTOR_SIZE);
				data_read(_fs, inode_which_block_group, (gdp[inode_which_block_group].start_block_of_block_bitmap + i), sector); //데이터 블럭을 읽어옴.


				for(UINT32 j = 0; (j < (MAX_SECTOR_SIZE * 8)) && ((j + i * (MAX_SECTOR_SIZE * 8)) < (_fs->sb.block_per_group - _fs->sb.first_data_block_each_group)); j++)	//j : 비트맵 내에서 비트맵의 오프셋, 즉 블록 그룹내의 블록 번호 의미
				{
					if (sector & 1)			  //사용중이면 1, 사용중이지 않으면 0
						sector = sector >> 1; //비트하나씩 땡겨가며 가정 처음 부터 탐색.
					else					  //사용중이지 않은 데이터 블록을 찾음
					{
						// result 찾은 섹터, 섹터당 비트 개수, 그리고 찾았던 비트 숫자가 블럭 번호이므로 계산해서 result에 저장.
						result = BOOT_BLOCK + (i * MAX_SECTOR_SIZE * 8 + j) + (inode_which_block_group * (_fs->sb.block_per_group)) + (_fs->sb.first_data_block_each_group);
						return result;
					}
				}
			}

			return EXT2_ERROR; // 이 루프에서 못 찾은경우 에러 리턴 -> 루프 검증 필요 하다는 뜻
		}
		else //아이노드가 있는 블럭 그룹에 할당가능한 데이터블럭이 존재하지 않는 경우, 다른 블럭 그룹에서 사용가능한 블럭번호를 계산해 리턴.
		{
			while (!(gdp[k].free_blocks_count)) //사용가능한 데이터 블럭이 없는 블럭 그룹은 값이 0이기 때문에, 계속 돌게된다. 빈 공간이 있으면 나옴. 처음 블럭그룹부터 탐색
				k++;

			//돌아다니다가 사용가능한 데이터 블럭이 있는 블럭 그룹을 찾은 경우. 블럭 비트맵을 참고해서 데이터 블록 받아서 리턴.
			for (UINT32 i = 0; i < sector_num_per_block; i++) //블럭당 섹터 수 만큼 섹터를 읽는다. i: 현재 처리중인 섹터 번호.
			{
				ZeroMemory(sector, MAX_SECTOR_SIZE);
				data_read(_fs, k, gdp[k].start_block_of_block_bitmap + i, sector); //데이터 블럭을 읽어옴.

				for (UINT32 j = 0; (j < MAX_SECTOR_SIZE * 8) && ((j + i * (MAX_SECTOR_SIZE * 8)) < (_fs[k]->sb.block_per_group - _fs[k]->sb.first_data_block_each_group)); j++)
				{
					if (sector & 1)			  //사용중이면 1, 사용중이지 않으면 0
						sector = sector >> 1; //비트하나씩 땡겨가며 가정 처음 부터 탐색.
					else					  //사용중이지 않은 데이터 블록을 찾음
					{
						// result 찾은 섹터, 섹터당 비트 개수, 그리고 찾았던 비트 숫자가 블럭 번호이므로 계산해서 result에 저장.
						result = BOOT_BLOCK + (i * MAX_SECTOR_SIZE * 8 + j) + (k * (_fs->sb.block_per_group)) + (_fs[k]->sb.first_data_block_each_group);
						return result;
					}
				}
			}

			return EXT2_ERROR; // 이 루프에서 못 찾은경우 에러 리턴 -> 루프 검증 필요 하다는 뜻
		}
	}

	return EXT2_ERROR; //슈퍼블럭 전체에서 할당가능한 데이터 블럭이 없는 경우 에러 발생.
}

unsigned char toupper(unsigned char ch); //to upper 즉 대문자로 바꾸는 함수 같은데 c 라이브러리에 있는 함수인듯
int isalpha(unsigned char ch);			 //알파벳 확인 함수
int isdigit(unsigned char ch);			 //숫자 확인 함수

void upper_string(char *str, int length) //상위 몇비트를 대문자로 바꾸는 함수
{
	while (*str && length-- > 0)
	{
		*str = toupper(*str);
		str++;
	}
}

int format_name(EXT2_FILESYSTEM *fs, char *name) //파일 이름의 형식이 올바른지 체크하는 함수
{
	UINT32 i, length;
	UINT32 extender = 0, nameLength = 0;
	UINT32 extenderCurrent = 8;
	BYTE regularName[MAX_ENTRY_NAME_LENGTH];

	memset(regularName, 0x20, sizeof(regularName));
	length = strlen(name);

	if (strncmp(name, "..", 2) == 0)
	{
		memcpy(name, "..         ", 11);
		return EXT2_SUCCESS;
	}
	else if (strncmp(name, ".", 1) == 0)
	{
		memcpy(name, ".          ", 11);
		return EXT2_SUCCESS;
	}
	else
	{
		upper_string(name, MAX_ENTRY_NAME_LENGTH);

		for (i = 0; i < length; i++)
		{
			if (name[i] != '.' && !isdigit(name[i]) && !isalpha(name[i]))
				return EXT2_ERROR;

			if (name[i] == '.')
			{
				if (extender)
					return EXT2_ERROR;
				extender = 1;
			}
			else if (isdigit(name[i]) || isalpha(name[i]))
			{
				if (extender)
					regularName[extenderCurrent++] = name[i];
				else
					regularName[nameLength++] = name[i];
			}
			else
				return EXT2_ERROR;
		}

		if (nameLength > 8 || nameLength == 0 || extenderCurrent > 11)
			return EXT2_ERROR;
	}

	memcpy(name, regularName, sizeof(regularName));
	return EXT2_SUCCESS;
}

/*
	같은 이름의 디렉터리 엔트리가 있는지 찾는 함수 (NULL인 경우는 아무 유효한 엔트리를 찾음)
	name이 존재하는 경우 : EXT2_SUCCESS 반환, 찾은 ENTRY로 retEntry가 가리키는 부분 초기화
	name이 존재하지 않는 경우 : EXT2_ERROR 반환
*/
int lookup_entry(EXT2_FILESYSTEM *fs, const int inode, const char *name, EXT2_NODE *retEntry)
{
	INODE	inodeBuffer;
	if (get_inode(fs, inode, &inodeBuffer) == EXT2_ERROR)
		return EXT2_ERROR;

	if (inode == 2) // 루트 디렉터리
		return find_entry_on_root(fs, inodeBuffer, name, retEntry);
	else
		return find_entry_on_data(fs, inodeBuffer, name, retEntry);
}

// 섹터(데이터 블록)에서 formattedName을 가진 엔트리를 찾아 그 위치를 number에 저장
int find_entry_at_sector(const BYTE *sector, const BYTE *formattedName, UINT32 begin, UINT32 last, UINT32 *number) //sector로 기준점이 들어오면, begin과 last로 섹터 범위를 지정해서 지정된 섹터만큼에서 검색하는걸까
{
	// 섹터 내부의 엔트리를 루프로 돌면서 formattedName과 이름이 같은 엔트리 검색
	// 있으면 number변수에 섹터 내에서의 위치를 저장하고, EXT2_SUCCESS 리턴
	EXT2_DIR_ENTRY*   dir;
	UINT	i;

	UINT max_entries_Per_Sector = MAX_SECTOR_SIZE / sizeof(EXT2_DIR_ENTRY);	//최대 섹터 크기를 디렉터리 엔트리 크기로 나누어서 섹터에 들어갈 수 있는 디렉터리 엔트리 개수를 구한다.
	dir = ((EXT2_DIR_ENTRY*)sector + begin);	//디렉토리 엔트리 주소를 sector로 받아서 dir에 저장하고 dir로 이용

	for (i = begin; i <= last; i++)
	{
		if (formattedName == NULL) // 이름에 상관없이 유효한 엔트리의 위치를 찾음
		{
			if( dir->name[0] != DIR_ENTRY_FREE && dir->name[0] != DIR_ENTRY_NO_MORE )
			{
				number = i;
				return EXT2_SUCCESS;
			}
		}
		else // 찾는 이름이 설정된 경우
		{
			// 삭제된 엔트리나 마지막 엔트리를 찾는 경우 첫번째 바이트만 비교
			if ( ( formattedName[0] == DIR_ENTRY_FREE || formattedName[0] == DIR_ENTRY_NO_MORE ) & ( formattedName[0] == dir->name[0] ) )
			{
				number = i;
				return EXT2_SUCCESS;
			}

			if(strcmp(dir->name, formattedName))	//비어있는 공간도 아니고 실제로 디렉터리 엔트리가 있으면 이름 비교
			{
				number = i;				//결과를 찾으면 number에 번호 기록후 리턴
				return  EXT2_SUCCESS;	//EXT2_SUCCESS 리턴
			}
		}

		if (dir->name[0] == DIR_ENTRY_NO_MORE)	//더 이상 디렉터리 엔트리가 없으면. 루프 나옴.
		{
			number = i;
			return -2;	//섹터 끝까지 보기전에 디렉터리 엔트리끝이 나왔으므로 에러.
		}

		dir++;	//결과 못 찾으면 계속 돔
	}

	number = i;
	return EXT2_ERROR;	//여기까지 오는건 다 돌았는데 못 찾는 경우이므로 EXT2_ERROR 리턴
}

// 루트 디렉터리 영역에서 formattedName의 엔트리 검색해서 EXT2_NODE* ret에 저장 (eunseo)
int find_entry_on_root(EXT2_FILESYSTEM *fs, INODE inode, char *formattedName, EXT2_NODE *ret)
{
	BYTE	sector[MAX_SECTOR_SIZE];		// 루트 디렉터리의 엔트리를 저장하는 섹터
	UINT32	number;							// formattedName을 가진 엔트리가 섹터 내에서 몇번째 엔트리인지
	UINT32	entriesPerSector, lastEntry;	// entriesPerSector: 섹터 당 엔트리 수, lastEntry: 탐색할 마지막 엔트리
	INT32	result;
	EXT2_DIR_ENTRY*	entry;

	read_root_sector(fs, sector); // 루트 디렉터리의 섹터단위 데이터를 sector 버퍼에 write
	entry = (EXT2_DIR_ENTRY*)sector; // 섹터의 시작주소
	
	entriesPerSector = fs->disk->bytesPerSector / sizeof( EXT2_DIR_ENTRY ); // 섹터 당 엔트리 수
	lastEntry = entriesPerSector - 1; // 탐색할 마지막 엔트리
	result = find_entry_at_sector(sector, formattedName, 0, lastEntry, &number); // 섹터에서 formattedName을 가진 엔트리를 찾아 그 위치를 number에 저장

	if( result == -1 || result == -2) // formattedName을 가진 엔트리가 없거나 더 이상 엔트리가 없다면 에러
		return EXT2_ERROR;
	else // 해당 엔트리를 찾았다면 ret에서 가리키는 EXT2_NODE를 entry 정보로 초기화
	{
		memcpy( &ret->entry, &entry[number], sizeof( EXT2_DIR_ENTRY ) );

		ret->location.group	= GET_INODE_GROUP(2);
		ret->location.block	= 1;
		ret->location.offset = number; // 블록 안에서의 offset

		ret->fs = fs;
	}

	return EXT2_SUCCESS;
}

// 데이터 영역에서 formattedName의 엔트리 검색 (eunseo)
int find_entry_on_data(EXT2_FILESYSTEM *fs, INODE first, const BYTE *formattedName, EXT2_NODE *ret)
{
	BYTE	sector[MAX_SECTOR_SIZE];	// 엔트리를 저장하는 섹터
	UINT32	i, block, number;			// block: inode 내에서 데이터블록의 위치 오프셋, number: 블록 내에서 formattedName을 가진 엔트리의 위치 오프셋
	UINT32	sectorsPerBlock;
	UINT32	entriesPerSector, beginEntry, lastEntry;	// beginEntry: 탐색할 시작 엔트리, lastEntry: 탐색할 마지막 엔트리
	INT32	blockNum;					// 데이터 블록 번호 (그룹에 상관 없이 고유)
	INT32	result;
	EXT2_DIR_ENTRY*	entry;

	sectorsPerBlock		= MAX_SECTOR_SIZE / MAX_BLOCK_SIZE; // 블록 당 섹터 수
	entriesPerSector	= fs->disk->bytesPerSector / sizeof( EXT2_DIR_ENTRY ); // 섹터 당 엔트리 수
	lastEntry			= entriesPerSector - 1; // 마지막 엔트리

	for (block = 0; block < first.blocks; block++) // 데이터 블록 단위로 검색 
	{
		blockNum = get_data_block_at_inode(fs, first, block); // 데이터 블록 번호 (그룹에 상관 없이 고유)
		data_read(fs, 0, blockNum, sector); // 데이터 블록의 데이터를 sector 버퍼에 저장
		entry = (EXT2_DIR_ENTRY*)sector; // 섹터의 시작주소

		for (i = 0; i < sectorsPerBlock; i++) // 섹터단위로 검색
		{
			beginEntry = i * entriesPerSector; // 탐색할 시작 엔트리
			lastEntry = beginEntry + entriesPerSector - 1; // 탐색할 마지막 엔트리
			result = find_entry_at_sector(sector, formattedName, beginEntry, lastEntry, &number); // 섹터에서 formattedName을 가진 엔트리를 찾아 그 위치를 number에 저장

			if( result == -1 ) // 해당 섹터에 formattedName을 가진 엔트리가 없다면 다음 섹터에서 검색
				continue; 
			else // 현재 섹터에서 찾았거나 마지막 엔트리까지 검색한 경우
			{
				if( result == -2 ) // 더 이상 엔트리가 없다면 에러
					return EXT2_ERROR;
				else // 해당 엔트리를 찾았다면 ret에서 가리키는 FAT_NODE를 entry 정보로 초기화
				{
					memcpy( &ret->entry, &entry[number], sizeof( EXT2_DIR_ENTRY ) ); // 엔트리의 내용을 복사

					ret->location.group	= 0; // ?
					ret->location.block	= blockNum;
					ret->location.offset	= number;

					ret->fs = fs;
				}

				return EXT2_SUCCESS;
			}
		}
	}
}

// inode table에서 inode number에 대한 메타데이터를 inodeBuffer에 저장
int get_inode(EXT2_FILESYSTEM *fs, const UINT32 inode, INODE *inodeBuffer)
// 각 블록 그룹마다 할당되는 아이노드 번호가 정해져 있다고 가정(아이노드 테이블에 저장)
{
	UINT32 groupNumber;							// 해당 아이노드가 속해 있는 블록 그룹 번호
	UINT32 groupOffset;								// 해당 블록 그룹에서의 아이노드 테이블의 위치 + 아이노드 테이블에서 몇 번째 블록인지(블록 단위 offset)
	UINT32 blockOffset;							// 블록에서 몇 번재 아이노드인지
	BYTE blockBuffer[cal_block_size(fs->sb.log_block_size)];		// 한 블록을 읽어오기 위한 버퍼

	if (inode>fs->sb.max_inode_count||inode<1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}
	
	get_inode_location(fs, inode, &groupNumber, &groupOffset, &blockOffset);
	ZeroMemory(blockBuffer, cal_block_size(fs->sb.log_block_size));		
		
	if(block_read(fs,groupNumber, groupOffset, blockBuffer))	
	// 해당 아이노드가 속해 있는 블록을 읽어옴(data_read 함수에서는 섹터 단위로 탐색하고 섹터 단위로 읽음으로 sectorCount만큼 곱해줌)
	// 섹터 단위로 읽은 후 block에 섹터 단위로 순서대로 저장
	// 부트 섹터는 data_read 함수에서 1로 계산
	{																	
		return EXT2_ERROR;
	}
	
	memcpy(inodeBuffer, &(blockBuffer[blockOffset*128]), 128);	
	// inodeBuffer에 해당 아이노드가 들어있는 메모리만 복사해줌

	return EXT2_SUCCESS;
}

int get_inode_location(EXT2_FILESYSTEM* fs, const UINT32 inode, UINT32 *groupNumber, UINT32 *groupOffset, UINT32 *blockOffset)
{	// 아이노드 번호를 받아서 블록 그룹 번호, 그룹 내 아이노드 테이블+테이블에서의 offset, 블록 내에서의 아이노드 offset을 인자에 저장 by seungmin
	UINT32 inodeTable;
	UINT32 inode_per_block;
	UINT32 tableOffset;

	if (inode>fs->sb.max_inode_count||inode<1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}

	inode_per_block = cal_inode_per_block(fs->sb.log_block_size);// 블록 크기에 따라 블록 당 아이노드 수 계산 - 아이노드의 크기를 128byte로 가정함 -> 다른 변수 set할 때도 이게 편할 듯
	*groupNumber = (inode-1)/fs->sb.inode_per_group;			// 해당 아이노드가 속해있는 블록그룹의 번호 계산(-1은 아이노드의 인덱스가 1부터 시작하기 때문)
	inodeTable = fs->gd.start_block_of_inode_table -1;
	// 해당 블록그룹에서의 아이노드 테이블 시작 위치 -> 수퍼블록에 들어있는 아이노드 테이블의 시작 블록(offset 개념) - 1
	/* 각각의 블록그룹마다 1 block의 수퍼블록, n block의 group_descriptor_table, 1 block의 blcok_bitmap, 1 block의 inode_bitmap을 가지고 있다
	   이 때 group_descriptor_table의 크기는 모두 동일할 것임으로 start_block_of_inode_table을 n+3으로 set해서 offset으로 사용(부트섹터는 data_read에서 더해줌)*/
	/* -1을 해준 이유 - 블록을 읽을 때는 시작 블록까지 포함에서 읽어야 함으로 첫 블록을 포함시켜 주기 위해*/
	tableOffset = (((inode-1)%fs->sb.inode_per_group)-1)/inode_per_block;	// 해당 아이노드 테이블에서의 offset(블록 단위)
	*groupOffset = inodeTable + tableOffset;
	*blockOffset = (((inode-1)%fs->sb.inode_per_group)-1) - (tableOffset*inode_per_block);		// 블록 내 offset(아이노드 개수 단위)

	return EXT2_SUCCESS;
}

int block_read(EXT2_FILESYSTEM* fs, unsigned int group, unsigned int block, unsigned char *blockBuffer)
{	// 인자로 받은 값들을 바탕으로 블록단위로 read  by seungmin
	UINT32 blockSize;							// 블록 사이즈
	UINT32 sectorCount;							// 한 블록 안에 몇 개의 섹터가 들어있는지

	blockSize=cal_block_size(fs->sb.log_block_size);	// 블록 사이즈 계산
	sectorCount=blockSize/MAX_SECTOR_SIZE;				// 블록 당 섹터 수 계산

	for (int i=0;i<sectorCount;i++)
	{
		if(data_read(fs, group, (block*sectorCount)+i, &(blockBuffer[MAX_SECTOR_SIZE*i])))
		{																	
			printf("Read failed\n");
			return EXT2_ERROR;
		}
	}
	return EXT2_SUCCESS;
}

int block_write(EXT2_FILESYSTEM* fs, unsigned int group, unsigned int block, unsigned char *blockBuffer)
{
	UINT32 blockSize;							// 블록 사이즈
	UINT32 sectorCount;							// 한 블록 안에 몇 개의 섹터가 들어있는지

	blockSize=cal_block_size(fs->sb.log_block_size);	// 블록 사이즈 계산
	sectorCount=blockSize/MAX_SECTOR_SIZE;			// 블록 당 섹터 수

	for (int i=0;i<sectorCount;i++)
	{
		if(data_write(fs, group, (block*sectorCount)+i, &(blockBuffer[MAX_SECTOR_SIZE*i])))
		{																	
			printf("Write failed\n");
			return EXT2_ERROR;
		}
	}
	return EXT2_SUCCESS;
}

// 루트 디렉터리의 섹터단위 데이터블록을 sector 버퍼에 write
int read_root_sector(EXT2_FILESYSTEM *fs, BYTE *sector) //루트 디렉터리에 관한 정보를 읽어옴. fs로 넘겨주면, sector에 담아줌
{
	UINT32 inode = 2;										 // 루트 디렉터리 inode number
	INODE inodeBuffer;										 // 아이노드 메타데이터
	SECTOR rootBlock;										 // 루트 디렉터리의 첫번째 데이터블록 번호
	get_inode(fs, inode, &inodeBuffer);						 // 루트 디렉터리의 메타데이터를 inodeBuffer에 저장
	rootBlock = get_data_block_at_inode(fs, inodeBuffer, 1); // 루트 디렉터리의 첫번째 데이터 블록 번호를 return

	return data_read(fs, 0, rootBlock, sector); // 루트 디렉터리의 데이터 블록의 데이터를 sector 버퍼에 저장
}


void get_block_location(EXT2_FILESYSTEM* fs, const UINT32 blockNumber, UINT32 *groupNumber, UINT32 *offset)
{	// 블록 번호를 받아서 블록 그룹 번호와 그룹 내 offset을 인자에 저장  by seungmin
	*groupNumber = (blockNumber-1)/fs->sb.block_per_group;		// 블록 번호의 블록 그룹 번호
	*offset = (blockNumber-1)%fs->sb.block_per_group;			// 블록 번호의 블록 그룹 기준 offset
}

int get_indirect_block_location_at_inode(EXT2_FILESYSTEM *fs, INODE inode, UINT32 number, UINT32 *groupNumber, UINT32 *groupOffset, UINT32 *blockOffset)
{
	UINT32 blockSize;			// 블록 크기
	UINT32 block;				// 블록 당 가질 수 있는 데이터 블록의 수
	UINT32 maxNumber;			// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수
	UINT32 count=12;			// 몇 번째 간접 블록인지 계산하기 위한 변수
	UINT32 offset;				// 간접 블록 내에서 몇 번째 블록인지
	BYTE blockBuffer[cal_block_size(fs->sb.log_block_size)];

/* inode
	if (inode>fs->sb.max_inode_count||inode<1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}
*/

	blockSize=cal_block_size(fs->sb.log_block_size);	// 블록 크기 설정
	block=(blockSize/4);						// 블록 당 가질 수 있는 데이터 블록의 수(4byte 단위임으로)
	maxNumber=12+(blockSize/4)+((blockSize/4)*(blockSize/4))+((blockSize/4)*(blockSize/4)*(blockSize/4));
	// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수 - 직접 블록 12개 + 간접 블록 + 2중 간접 블록+ 3중 간접 블록

	if (number<13||number>maxNumber)	
	{
		printf("Invalid block number\n");
		return EXT2_ERROR;
	}
	else
	{
		UINT32 buffer=number-12;		// 간접 블록에서 몇 번째 블록인지 계산하기 위해 -12		
		while(1)
		{
			if ((buffer - 1) / block == 0) // 해당 간접 블록에 속할 경우
			{
				offset=(buffer-1)%block;	
				offset++;					// offset은 해당 간접 블록에서 몇 번째 블록인지를 뜻하는 변수 -> ++연산
				break;
			}
			count++;						// 해당 간접 블록에 속하지 않을 경우 다음 간접블록으로 가서 검사
			buffer-=block;					// 이전 간접 블록의 데이터 블록 개수만큼 빼줌
			block*=(blockSize/4);			// 다음 간접 블록으로 넘어감으로 가르킬 수 있는 블록의 개수 업데이트
		}

		if (inode.block[count-1]<1)	
		{
			printf("The block is not allocated\n");
			return inode_data_empty;
		}
	}

	get_block_location(fs, inode.block[count-1], groupNumber, groupOffset);	// 블록 번호로 블록의 위치 찾아 인자에 저장
	ZeroMemory(blockBuffer,cal_block_size(fs->sb.log_block_size));									// 버퍼 초기화

	if(block_read(fs, groupNumber, groupOffset, blockBuffer))			// 간접 블록에서 가르키는 첫 번째 블록을 읽어옴
	{																	
		return EXT2_ERROR;
	}
	
	UINT32 temp;	
	UINT32 *blockNumber;	// 블록 번호를 저장하기 위한 변수
	for (int i=0;i<count-12;i++)										
	{	
		ZeroMemory(blockNumber,4);
		block/=(blockSize/4);		
		temp=(offset-1)/block;										// 해당 블록에서 4byte를 읽을 위치 계산
		memcpy(blockNumber,&(blockBuffer[temp*4]),4);				// 4byte를 읽어 bloclkNumber 변수에 읽은 블록 번호 저장
		if ((*blockNumber)<1)										// 블록 번호가 1보다 작으면 에러 처리
		{
			printf("The block is not allocated\n");
			*blockOffset=temp+1;
			return inode_data_empty;
		}
		get_block_location(fs, *blockNumber, groupNumber, groupOffset);
		ZeroMemory(blockBuffer,cal_block_size(fs->sb.log_block_size));		
		
		if(block_read(fs, *groupNumber, *groupOffset, blockBuffer))	// 읽어온 블록 번호를 이용해 간접 블록에서 가르키는 다음 블록을 읽어옴
		{																	
			return EXT2_ERROR;
		}
		offset-=(temp*block);				// 읽어온 블록보다 앞에 있는 블록의 수만큼 offset에서 빼줌
	}
	
	*blockOffset=offset;

	return EXT2_SUCCESS;
}

// inode의 number번째 데이터 블록 번호를 return
int get_data_block_at_inode(EXT2_FILESYSTEM *fs, INODE inode, UINT32 number)	//inode : 어떤 파일의 아이노드, number : inode 구조체의 block필드에서 몇번째 데이터 블록을 불러올지 결정하는 변수 인듯
{	
	UINT32 blockSize;			// 블록 크기
	UINT32 block;				// 블록 당 가질 수 있는 데이터 블록의 수
	UINT32 maxNumber;			// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수
	UINT32 groupNumber;			// 블록 그룹 번호 계산
	UINT32 groupOffset;			// 블록 그룹에서의 offset(블록 단위)
	UINT32 blockOffset;			// 블록 내에서의 offset
	UINT32 *blockNumber;	// 블록 번호를 저장하기 위한 변수
	INT32 reTurn;
	BYTE blockBuffer[cal_block_size(fs->sb.log_block_size)];

/*
	if (inode>fs->sb.max_inode_count||inode<1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}
*/
	
	blockSize=cal_block_size(fs->sb.log_block_size);	// 블록 크기 설정(Byte단위)
	block=(blockSize/4);						// 블록 당 가질 수 있는 데이터 블록의 수(4byte 단위임으로)
	maxNumber=12+(blockSize/4)+((blockSize/4)*(blockSize/4))+((blockSize/4)*(blockSize/4)*(blockSize/4));
	// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수 - 직접 블록 12개 + 간접 블록 + 2중 간접 블록+ 3중 간접 블록

	if (number<1||number>maxNumber)	
	{
		printf("Invalid block number\n");
		return EXT2_ERROR;
	}
	else if (number<13)					// 직접 블록이 가르킬 수 있는 범위 안일 경우 바로 인덱싱하여 return
	{
		if (inode.block[number-1]<1)	// 아이노드 생성시 block 필드 값들을 0으로 초기화시켰을 경우
		{
			printf("The block is not allocated\n");
			return inode_data_empty;
		}
		return inode.block[number-1];	// 인자로 인덱스가 아니라 몇 번째 데이터 블록인지가 넘어오는 듯 해 인덱스가 될 수 있도록 -1
	}
	else
	{	
		reTurn=get_indirect_block_location_at_inode(fs, inode, number, &groupNumber, &groupOffset, &blockOffset);
		if (reTurn==EXT2_ERROR)
		{
			return EXT2_ERROR;
		}
		else if(reTurn==inode_data_empty)
		{
			return inode_data_empty;
		}
		else
		{
			ZeroMemory(blockBuffer,cal_block_size(fs->sb.log_block_size));		
		
			if(block_read(fs, groupNumber, groupOffset, blockBuffer))	// 읽어온 블록 번호를 이용해 간접 블록에서 가르키는 블록을 읽어옴
			{																	
				return EXT2_ERROR;
			}
			ZeroMemory(blockNumber,4);
			memcpy(blockNumber, &(blockBuffer[(blockOffset-1)*4]),4);	// 마지막으로 읽어온 블록에서 4byte를 읽어 blockNumber 변수에 저장
			if ((*blockNumber)<1)
			{
				printf("The block is not allocated\n");
				return inode_data_empty;
			}
			return (*blockNumber);
		}
	}
  	/*
	추가 - ext2 write에서 i=1 후 ++i를 하는 이유 -> 위의 주석에서 볼 수 있듯 인자로 넘어오는 number는 인덱스가 아니라 몇 번째 블록인지를 뜻 하는 것 같다
	ext2_write에서는 currentBlock으로 i_block[0]의 값을 넣은 후 다음 블록을 찾을 때 이 함수의 number 인자로 i=1 후 ++i를 해서 2를 넘겨주게 되는데,
	이것이 2번 째 블록을 읽어달라는 요청을 의미하는 것 같다(i_block[0] 값을 가져옴으로써 첫 번째 블록은 이미 읽었음으로)
	seungmin */
  
	//만약 number이 1~12이 들어오면 직접 데이터 블록 받아서 리턴
	//13이면 간접 블록 들어가서 안에 어떤 데이터 블록을 가리키는지 가져올 필요가 있음
	//만약 넘버가 14라면, 이중 간접 블록이 아니라 아이노드 13번째 구조체가 가리키는 간접 블록을 먼저 들어가서 거기서 13번재 다음 블록을 찾을 것으로 예상
	//블록 그룹 계산하지 않은 그냥 블록 그룹 내에서 블록 번호 리턴하는 것으로 생각
}

int ext2_read_superblock(EXT2_FILESYSTEM *fs, EXT2_NODE *root) //슈퍼블록을 읽는 함수 인것 같다. root는 읽은 슈퍼블록을 담을 곳을 인자로 넘겨 받음
{
	INT result;					  //결과를 리턴을 위한 변수
	BYTE sector[MAX_SECTOR_SIZE]; //섹터크기 만큼 바이트 설정. 연속적으로 고정된 공간 할당 위해 정적배열 사용 (1024바이트)

	if (fs == NULL || fs->disk == NULL) //fs가 지정되지 않으면 에러
	{
		WARNING("DISK OPERATIONS : %p\nEXT2_FILESYSTEM : %p\n", fs, fs->disk);
		return EXT2_ERROR;
	}

	meta_read(fs, 0, SUPER_BLOCK, sector);			   //
	memcpy(&fs->sb, sector, sizeof(EXT2_SUPER_BLOCK)); //첫번째 인자가 목적지, 두번째 인자가 어떤 것을 복사할지, 세번째는 크기
	meta_read(fs, 0, GROUP_DES, sector);			   //그룹 디스크립터 읽는듯.
	memcpy(&fs->gd, sector, sizeof(EXT2_GROUP_DESCRIPTOR));
	//디스크에서 슈퍼블록 정보 입력을 받아와서 인자로 들어온 fs의 슈퍼블록을 업데이트하는 과정으로 생각됨.

	if (fs->sb.magic_signature != 0xEF53) //슈퍼블럭인지 판단하는 필드. 고유값 확인으로.
		return EXT2_ERROR;

	ZeroMemory(sector, sizeof(MAX_SECTOR_SIZE)); //메모리 초기화.
	if (read_root_sector(fs, sector))			 //슈퍼블록이 루트 디렉터리를 제대로 가리키는지 체크하기 위한 부분이 아닐까 생각.
		return EXT2_ERROR;

	ZeroMemory(root, sizeof(EXT2_NODE));
	memcpy(&root->entry, sector, sizeof(EXT2_DIR_ENTRY)); //앞의 테스트를 거치면 제대로 된 슈퍼블록이므로 root에 담아서 리턴해준다.
	root->fs = fs;

	return EXT2_SUCCESS;
}

UINT32 get_free_inode_number(EXT2_FILESYSTEM *fs) //비어있는 아이노드 번호를 출력하는 것 같다.
{
	//EXT2_FILESYSTEM 구조체를 보면 슈퍼블록 그룹디스크립터 있음.그룹 디스크립터는 비어있는 아이노드 수, 아이노드 테이블 시작 주소, 비트맵 시작주소 가지고 있음
	//일단 아이노드 수 체크해서 없으면 에러, 있으면 비트맵 비교를 통해 가장 앞에 비어있는 아이노드 번호를 리턴 해주어야 한다고 생각.
	//리턴 형이 unsigned int 32비트 형식이니까 그대로 아이노드 번호를 리턴해주어야 할 것으로 생각.

	EXT2_GROUP_DESCRIPTOR *gdp; //group descriptor pointer라는 뜻
	EXT2_FILESYSTEM *_fs = fs;
	UINT32 result;
	BYTE sector[MAX_SECTOR_SIZE];	//아이노드 비트맵을 가져와서 저장할 공간.
	UINT32 sector_num_per_block = MAX_BLOCK_SIZE / MAX_SECTOR_SIZE;			//블럭당 섹터의 개수
	UINT32 k = 0;	//블럭 그룹 번호 저장

	gdp = (EXT2_GROUP_DESCRIPTOR*)_fs->gd; // error - cannot convert to a pointer type


	//먼저 슈퍼블럭값을 통해 볼륨 전체에 사용가능한 아이노드 저장공간이 있는지 확인.
	if (_fs->sb.free_inode_count) //볼륨 내에 할당 가능한 아이노드 공간이 있는 경우.
	{
		if (gdp[_fs->sb.block_group_number].free_inodes_count) //같은 그룹내에 아이노드를 할당할 공간이 있는 경우.
		{
			//가장 빨리 비어있는 아이노드 빈자리를 아이노드 비트맵을 통해 구한다.
			for (UINT32 i = 0; i < sector_num_per_block; i++) //블럭당 섹터 수 만큼 섹터를 읽는다. i: 현재 처리중인 섹터 번호.
			{
				ZeroMemory(sector, MAX_SECTOR_SIZE);
				data_read(_fs, _fs->sb.block_group_number, (gdp[_fs->sb.block_group_number].start_block_of_inode_bitmap + i), sector); //데이터 블럭을 읽어옴.

				for (UINT32 j = 0; (j < (MAX_SECTOR_SIZE * 8)) && ((j + i * (MAX_SECTOR_SIZE * 8)) < _fs->sb.inode_per_group); j++) //j : 비트맵 내에서 비트맵의 오프셋, 즉 블록 그룹내의 블록 번호 의미
				{
					if (sector & 1)			  //사용중이면 1, 사용중이지 않으면 0
						sector = sector >> 1; //비트하나씩 땡겨가며 가정 처음 부터 탐색.
					else					  //사용중이지 않은 데이터 블록을 찾음
					{
						//앞에 그룹 수 * 그룹 내 아이노드 수 + 비트맵 몇번째 섹터 더했는지, 비트맵에서 j가 몇번인지.
						result = (_fs->sb.inode_per_group * _fs->sb.block_group_number) + (i * MAX_SECTOR_SIZE * 8) + j;
						return result;
					}
				}
			}

			return EXT2_ERROR; // 이 루프에서 못 찾은경우 에러 리턴 -> 루프 검증 필요 하다는 뜻
		}
		else //같은 그룹내에 아이노드를 할당할 공간이 없는 경우.
		{
			while (!(gdp[k].free_inodes_count)) //사용가능한 아이노드가 없는 경우 값이 0이기 때문에, 계속 돌게된다. 빈 공간이 있으면 나옴. 처음 블럭그룹부터 탐색
				k++;

			//가장 빨리 비어있는 아이노드 빈자리를 아이노드 비트맵을 통해 구한다.
			for (UINT32 i = 0; i < sector_num_per_block; i++) //블럭당 섹터 수 만큼 섹터를 읽는다. i: 현재 처리중인 섹터 번호.
			{
				ZeroMemory(sector, MAX_SECTOR_SIZE);
				data_read(_fs, k, (gdp[k].start_block_of_inode_bitmap + i), sector); //데이터 블럭을 읽어옴.

				for (UINT32 j = 0; (j < (MAX_SECTOR_SIZE * 8)) && ((j + i * (MAX_SECTOR_SIZE * 8)) < _fs->sb.inode_per_group); j++) //j : 비트맵 내에서 비트맵의 오프셋, 즉 블록 그룹내의 블록 번호 의미
				{
					if (sector & 1)			  //사용중이면 1, 사용중이지 않으면 0
						sector = sector >> 1; //비트하나씩 땡겨가며 가정 처음 부터 탐색.
					else					  //사용중이지 않은 데이터 블록을 찾음
					{
						//앞에 그룹 수 * 그룹 내 아이노드 수 + 비트맵 몇번째 섹터 더했는지, 비트맵에서 j가 몇번인지.
						result = (_fs->sb.inode_per_group * k) + (i * MAX_SECTOR_SIZE * 8) + j;
						return result;
					}
				}
			}

			return EXT2_ERROR; // 이 루프에서 못 찾은경우 에러 리턴 -> 루프 검증 필요 하다는 뜻
		}
	}

	return EXT2_ERROR; //볼륨내에 할당가능한 아이노드 공간이 없음.
}


int set_inode_onto_inode_table(EXT2_FILESYSTEM *fs, const UINT32 inode_num, INODE * inode_to_write)	// 인자로 받은 아이노드를 아이노드 번호에 해당하는 위치에 업데이트
{
	UINT32 groupNumber;			// 해당 아이노드의 블록 그룹 번호
	UINT32 groupOffset;			// 해당 아이노드의 그룹 내 offset(블록 단위)
	UINT32 blockOffset;			// 해당 아이노드의 블록 내 offset(아이노드 단위)
	BYTE blockBuffer[cal(fs->sb.log_block_size)];			// 블록을 저장할 버퍼

	if (inode_num>fs->sb.max_inode_count||inode_num<1)
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}

	if(get_inode_location(fs, inode_num, &groupNumber, &groupOffset, &blockOffset))		// 아이노드의 위치를 찾아 인자에 저장
	{
		return EXT2_ERROR;
	}
	ZeroMemory(blockBuffer, cal(fs->sb.log_block_size));								// 버퍼 초기화
	if(block_read(fs, groupNumber, groupOffset, blockBuffer))							// 해당 아이노드가 속해 있는 블록을 읽어옴
	{
		return EXT2_ERROR;
	}
	
	memcpy(&(blockBuffer[blockOffset*128]) ,inode_to_write, 128);						// 읽어온 블록에 수정한 아이노드 저장

	if(block_write(fs, groupNumber, groupOffset, blockBuffer))							// 수정한 블록을 다시 업데이트
	{
		return EXT2_ERROR;
	}

	return EXT2_SUCCESS;
	//호출하는 쪽에서 get_free_inode_number을 통해서 비어있는 아이노드 번호를 알아내서 which_inode_num_to_write로 넘겨줄것으로 예상
	//호출하는 쪽에서 새로 생성되는 파일에 대한 아이노드 구조체를 새로 만듬. 그리고 그 구조체를 인자로 넘겨줄것으로 예상됨
	//이 함수에서는 그러면 아이노드 테이블에 아이노드 정보를 기록하고 성공여부를 리턴할 것으로 예상됨.
}

// 디렉터리의 엔트리들을 리스트에 담음
int ext2_read_dir(EXT2_NODE *dir, EXT2_NODE_ADD adder, void *list)
{
	BYTE sector[MAX_SECTOR_SIZE];
	INODE *inodeBuffer;
	UINT32 inode;
	int i, result, num;

	inodeBuffer = (INODE *)malloc(sizeof(INODE));

	ZeroMemory(sector, MAX_SECTOR_SIZE);
	ZeroMemory(inodeBuffer, sizeof(INODE));

	result = get_inode(dir->fs, dir->entry.inode, inodeBuffer); // inode number에 대한 메타데이터를 inodeBuffer에 저장
	if (result == EXT2_ERROR)
		return EXT2_ERROR;

	// 데이터 블록 단위로 루프
	for (i = 0; i < inodeBuffer->blocks; ++i)
	{
		num = get_data_block_at_inode(dir->fs, *inodeBuffer, i + 1); // inodeBuffer의 number(i+1)번째 데이터 블록 번호를 return
		data_read(dir->fs, 0, num, sector);							 // 디스크 영역에서 현재 블록그룹의 num번째 데이터 블록의 데이터를 sector 버퍼에 읽어옴

		if (dir->entry.inode == 2)									 // 루트 디렉터리
			read_dir_from_sector(dir->fs, sector + 32, adder, list); // 디렉터리 정보를 담은 sector 버퍼를 읽어 엔트리를 list에 추가 (+32?)
		else
			read_dir_from_sector(dir->fs, sector, adder, list); // 디렉터리 정보를 담은 sector 버퍼를 읽어 엔트리를 list에 추가
	}

	return EXT2_SUCCESS;
}

int read_dir_from_sector(EXT2_FILESYSTEM *fs, BYTE *sector, EXT2_NODE_ADD adder, void *list) //블록단위가 아닌 섹터 단위에서 디렉토리 엔트리 리스트들을 가져오는 함수 인듯.
{
	UINT i, max_entries_Per_Sector;
	EXT2_DIR_ENTRY *dir;
	EXT2_NODE node;

	max_entries_Per_Sector = MAX_SECTOR_SIZE / sizeof(EXT2_DIR_ENTRY); //최대 섹터 크기를 디렉터리 엔트리 크기로 나누어서 섹터에 들어갈 수 있는 디렉터리 엔트리 개수를 구한다.
	dir = (EXT2_DIR_ENTRY *)sector;									   //디렉토리 엔트리 주소를 sector로 받아서 dir에 저장하고 dir로 이용

	for (i = 0; i < max_entries_Per_Sector; i++)
	{
		if (dir->name[0] == DIR_ENTRY_FREE) //탐색하다가 중간에 비어 있는 공간이 있으면 그냥 통과. fragmentation일 수도 있으니.
			;
		else if (dir->name[0] == DIR_ENTRY_NO_MORE) //더 이상 디렉터리 엔트리가 없으면. 루프 나옴.
			break;
		else //비어있는 공간도 아니고 실제로 디렉터리 엔트리가 있으면 list에 저장하고 다음 엔트리가 있는지 탐색
		{
			node.fs = fs;
			node.entry = *dir;
			adder(fs, list, &node); //adder 함수가 구현되어 있지 않은것 같은데 구현 필요한듯. adder를 하면 list에 node.entry의 정보가 달려오지 않을까.
		}
		dir++;
	}

	return (i == max_entries_Per_Sector ? 0 : -1); //끝가지 돌면 0리턴, 아니면 -1리턴.
}

char *my_strncpy(char *dest, const char *src, int length)
{
	while (*src && *src != 0x20 && length-- > 0)
		*dest++ = *src++;

	return dest;
}

// parent에 새로운 디렉터리 생성
int ext2_mkdir(const EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry)
{
	EXT2_NODE dotNode, dotdotNode;
	DWORD firstCluster;
	BYTE name[MAX_NAME_LENGTH];
	int result;
	int i;

	strcpy((char *)name, entryName); // 입력한 이름 복사

	if (format_name(parent->fs, (char *)name)) // EXT2 버전의 형식에 맞게 이름 수정
		return EXT2_ERROR;

	/* newEntry */
	ZeroMemory(retEntry, sizeof(EXT2_NODE));
	memcpy(retEntry->entry.name, name, MAX_ENTRY_NAME_LENGTH);		 // name을 복사
	retEntry->entry.name_len = strlen((char *)retEntry->entry.name); // name의 길이 저장
	retEntry->fs = parent->fs;										 // EXT2_FILESYSTEM 복사

	result = insert_entry(parent->entry.inode, retEntry, FILE_TYPE_DIR); // 부모 디렉터리에 새로운 엔트리(retEntry) 추가
	if (result == EXT2_ERROR)											 // 에러 발생시 종료
		return EXT2_ERROR;

	expand_block(parent->fs, retEntry->entry.inode); // 새로운 엔트리(retEntry)의 데이터블록 할당

	/* dotEntry */
	ZeroMemory(&dotNode, sizeof(EXT2_NODE));
	memset(dotNode.entry.name, 0x20, 11);						  // 이름을 space로 초기화
	dotNode.entry.name[0] = '.';								  // 엔트리 이름 설정 '.'
	dotNode.fs = retEntry->fs;									  // 파일시스템 복사
	dotNode.entry.inode = retEntry->entry.inode;				  // retEntry의 아이노드 복사
	insert_entry(retEntry->entry.inode, &dotNode, FILE_TYPE_DIR); // 새로운 디렉터리(retEntry)에 dotEntry 추가

	/* dotdotEntry */
	ZeroMemory(&dotdotNode, sizeof(EXT2_NODE));
	memset(dotdotNode.entry.name, 0x20, 11); // 이름을 space로 초기화
	dotdotNode.entry.name[0] = '.';			 // 엔트리 이름 설정 '..'
	dotdotNode.entry.name[1] = '.';
	dotdotNode.entry.inode = parent->entry.inode;					 // 부모 디렉터리의 아이노드 복사
	dotdotNode.fs = retEntry->fs;									 // 파일시스템 복사
	insert_entry(retEntry->entry.inode, &dotdotNode, FILE_TYPE_DIR); // 새로운 디렉터리(retEntry)에 dotdotEntry 추가

	return EXT2_SUCCESS;
}

int meta_read(EXT2_FILESYSTEM *fs, SECTOR group, SECTOR block, BYTE *sector) //블록 그룹과 블럭을 입력하면 디스크에서 실제 몇번 블럭인지 앞에서 부터 다 계산해서 디스크에서 가져온다.
{
	const SECTOR BOOT_BLOCK = 1;
	SECTOR real_index = BOOT_BLOCK + group * fs->sb.block_per_group + block;

	return fs->disk->read_sector(fs->disk, real_index, sector);
}
int meta_write(EXT2_FILESYSTEM *fs, SECTOR group, SECTOR block, BYTE *sector) //블록 그룹과 블럭을 입력하면 디스크에서 실제 몇번 블럭인지 앞에서 부터 다 계산해서 디스크에 쓴다.
{
	const SECTOR BOOT_BLOCK = 1;
	SECTOR real_index = BOOT_BLOCK + group * fs->sb.block_per_group + block;

	return fs->disk->write_sector(fs->disk, real_index, sector);
}
int data_read(EXT2_FILESYSTEM *fs, SECTOR group, SECTOR block, BYTE *sector)
{
	const SECTOR BOOT_BLOCK = 1;
	SECTOR real_index = BOOT_BLOCK + group * fs->sb.block_per_group + block;

	return fs->disk->read_sector(fs->disk, real_index, sector); // 성공 여부 리턴 (disksim.c -> disksim_read)
}
int data_write(EXT2_FILESYSTEM *fs, SECTOR group, SECTOR block, BYTE *sector) // 디스크의 데이터 블록에 작성
{
	const SECTOR BOOT_BLOCK = 1;
	SECTOR real_index = BOOT_BLOCK + group * fs->sb.block_per_group + block;

	return fs->disk->write_sector(fs->disk, real_index, sector); // 성공 여부 리턴 (disksim.c -> disksim_write)
}

int ext2_format(DISK_OPERATIONS *disk) //디스크를 ext2파일 시스템으로 초기화 하는 함수
{
	EXT2_SUPER_BLOCK sb;
	EXT2_GROUP_DESCRIPTOR gd;
	EXT2_GROUP_DESCRIPTOR gd_another_group;

	QWORD sector_num_per_group = (disk->numberOfSectors - 1) / NUMBER_OF_GROUPS; //디스크로부터 디스크 섹터 개수에 대한 정보를 읽어온다. 미리 설정된 그룹 개수만큼 나누어 그룹당 섹터 개수를 계산한다.
	int i, gi, j;
	const int BOOT_SECTOR_BASE = 1; //부트 섹터를 제외한 파일시스템의 기본번지 설정번지에 위치하도록

	char sector[MAX_SECTOR_SIZE]; //슈퍼블록을 넣을 메모리 공간을 할당받는다. 1KB만큼 하랑 받음

	// super block
	if (fill_super_block(&sb, disk->numberOfSectors, disk->bytesPerSector) != EXT2_SUCCESS) //메모리의 어떤 공간에 슈퍼블록 구조체에 관한 내용을 채워넣는다.
		return EXT2_ERROR;																	//실패시 에러

	ZeroMemory(sector, sizeof(sector));						//슈퍼블록이 들어갈 메모리 공간을 0으로 초기화 한다.
	memcpy(sector, &sb, sizeof(sb));						//아까 슈퍼블록 구조체에 넣어놨던 데이터를 미리 정해둔 크기의 메모리에 올린다.
	disk->write_sector(disk, BOOT_SECTOR_BASE + 0, sector); //disk의 동작을 통해 부트섹터를 제외한 디스크의 가장 처음 블록에 슈퍼블록을 저장한다.

	//Group Descriptor Table
	if (fill_descriptor_block(&gd, &sb, disk->numberOfSectors, disk->bytesPerSector) != EXT2_SUCCESS) //슈퍼블록을 디스크에 저장하는 과정과 마찬가지로 디스크에 블록 디스크립터를 저장한다.
		return EXT2_ERROR;

	gd_another_group = gd;
	gd_another_group.free_inodes_count = NUMBER_OF_INODES / NUMBER_OF_GROUPS;
	gd_another_group.free_blocks_count = sb.free_block_count / NUMBER_OF_GROUPS;

	ZeroMemory(sector, sizeof(sector));
	for (j = 0; j < NUMBER_OF_GROUPS; j++)
	{
		if (j == 0)
			memcpy(sector + j * sizeof(gd), &gd, sizeof(gd));
		else
			memcpy(sector + j * sizeof(gd_another_group), &gd_another_group, sizeof(gd_another_group));
	}

	disk->write_sector(disk, BOOT_SECTOR_BASE + 1, sector); //내부 원리 이해잘 못했음

	// block bitmap
	ZeroMemory(sector, sizeof(sector));

	sector[0] = 0xff;
	sector[1] = 0xff;
	sector[2] = 0x01;
	disk->write_sector(disk, BOOT_SECTOR_BASE + 2, sector);

	// inode bitmap
	ZeroMemory(sector, sizeof(sector));

	sector[0] = 0xff;
	sector[1] = 0x03;
	disk->write_sector(disk, BOOT_SECTOR_BASE + 3, sector);

	// inode table
	ZeroMemory(sector, sizeof(sector));

	for (i = 4; i < sb.first_data_block_each_group; i++)
		disk->write_sector(disk, BOOT_SECTOR_BASE + i, sector);

	// another group
	for (gi = 1; gi < NUMBER_OF_GROUPS; gi++)
	{
		sb.block_group_number = gi;

		ZeroMemory(sector, sizeof(sector));
		memcpy(sector, &sb, sizeof(sb));

		disk->write_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE, sector);

		ZeroMemory(sector, sizeof(sector));
		for (j = 0; j < NUMBER_OF_GROUPS; j++)
		{
			memcpy(sector + j * sizeof(gd), &gd, sizeof(gd));
		}
		disk->write_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE + 1, sector);

		// block bitmap
		ZeroMemory(sector, sizeof(sector));
		sector[0] = 0xff;
		sector[1] = 0xff;
		sector[2] = 0x01;
		disk->write_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE + 2, sector);

		//inode bitmap
		ZeroMemory(sector, sizeof(sector));

		disk->write_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE + 3, sector);

		// inode table
		ZeroMemory(sector, sizeof(sector));
		for (i = 4; i < sb.first_data_block_each_group; i++)
			disk->write_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE + i, sector);
	}

	PRINTF("max inode count                : %u\n", sb.max_inode_count);
	PRINTF("total block count              : %u\n", sb.block_count);
	PRINTF("byte size of inode structure   : %u\n", sb.inode_structure_size);
	PRINTF("block byte size                : %u\n", MAX_BLOCK_SIZE);
	PRINTF("total sectors count            : %u\n", NUMBER_OF_SECTORS);
	PRINTF("sector byte size               : %u\n", MAX_SECTOR_SIZE);
	PRINTF("\n");

	create_root(disk, &sb); //루트디렉터리를 만들어 슈퍼블럭과 연결시킨다.

	return EXT2_SUCCESS;
}
int ext2_create(EXT2_NODE *parent, char *entryName, EXT2_NODE *retEntry) //파일시스템에서 파일을 새로 생성할때 호출되는 함수.
{
	if ((parent->fs->gd.free_inodes_count) == 0)
		return EXT2_ERROR; //상성가능한 아이노드 공간이 없으면 에러
	UINT32 inode;
	BYTE name[MAX_NAME_LENGTH] = {
		0,
	};
	BYTE sector[MAX_BLOCK_SIZE];
	int result;

	strcpy(name, entryName);
	if (format_name(parent->fs, name) == EXT2_ERROR)
		return EXT2_ERROR; //이름이 형식에 맞지 않으면 에러

	/* newEntry */
	ZeroMemory(retEntry, sizeof(EXT2_NODE));
	memcpy(retEntry->entry.name, name, MAX_ENTRY_NAME_LENGTH);
	retEntry->fs = parent->fs;
	inode = parent->entry.inode;
	if ((result = lookup_entry(parent->fs, inode, name, retEntry)) == EXT2_SUCCESS)
		return EXT2_ERROR; //이미 디렉터리에 해당 이름의 파일이 있으면 에러
	else if (result == -2)
		return EXT2_ERROR;

	if (insert_entry(inode, retEntry, 0) == EXT2_ERROR)
		return EXT2_ERROR;
	return EXT2_SUCCESS;
}
int ext2_lookup(EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry) //entryName을 갖는 엔트리가 있는지 검색해 그 위치를 리턴
{
	EXT2_DIR_ENTRY_LOCATION begin;
	BYTE formattedName[MAX_NAME_LENGTH] = {
		0,
	};

	strcpy(formattedName, entryName);

	if (format_name(parent->fs, formattedName)) //파일이름이 올바른지 체크
		return EXT2_ERROR;

	return lookup_entry(parent->fs, parent->entry.inode, formattedName, retEntry);
}

UINT32 expand_block(EXT2_FILESYSTEM *fs, UINT32 inode_num) // inode에 새로운 데이터블록 할당
{
	INODE inodeBuffer;
	UINT32 groupNumber;					
	UINT32 groupOffset;
	UINT32 blockOffset;
	UINT32 available_block;				// 할당 가능한 데이터 블록 번호
	UINT32 checkFree=1;					// 해당 아이노드에서 몇 번째 데이터 블록인지
	UINT32 blockSize=cal_block_size(fs->sb.log_block_size);	// 블록 크기 설정
	UINT32 maxNumber = 12+(blockSize/4)+((blockSize/4)*(blockSize/4))+((blockSize/4)*(blockSize/4)*(blockSize/4));
	// 한 아이노드에서 가르킬 수 있는 데이터 블록의 최대 개수 - 직접 블록 12개 + 간접 블록 + 2중 간접 블록+ 3중 간접 블록
	INT32 reTurn;
	BYTE blockBuffer[blockSize];

	
	if (inode_num>fs->sb.max_inode_count||inode_num<1)		// 아이노드 번호 체크
	{
		printf("Invalid inode number\n");
		return EXT2_ERROR;
	}

	if(get_inode_location(fs, inode_num, &groupNumber, &groupOffset, &blockOffset))		// 해당 아이노드의 위치 읽어서 인자에 저장
	{
		return EXT2_ERROR;
	}

	ZeroMemory(blockBuffer, cal(fs->sb.log_block_size));	
	if(block_read(fs, groupNumber, groupOffset, blockBuffer))							// 해당 아이노드가 속한 블록 읽어서 blockBuffer에 저장
	{
		return EXT2_ERROR;
	}
	memcpy(&inodeBuffer, &(blockBuffer[blockOffset*128]), 128);							// 해당 블록에서 아이노드 읽어서 inodeBuffer에 저장
	
	while(get_data_block_at_inode(fs,inodeBuffer,checkFree)!=inode_data_empty)			// 아이노드에서 빈 데이터 블록을 찾을 때 까지
	{																					// get_data_block_at_inode 함수 호출
		if(checkFree>maxNumber)
		{
			printf("No empty block in this inode\n");
			return EXT2_ERROR;
		}
		checkFree++;
	}
	
	if(checkFree<13)																	// 직접 블록이 비어있을 경우
	{
		if(available_block = get_available_data_block(fs, inode_num)<0)					// 할당 가능한 데이터 블록 번호 읽어옴
		{
			printf("No empty block\n");
			return EXT2_ERROR;
		}
		memcpy(&(inodeBuffer[((checkFree-1)*4)+40]), &available_block, 4);				
		// 읽어온 아이노드에서 직접 블록 위치에 available_block 값 대입 - 40은 아이노드 구조체에서 block 필드의 위치, ((checkFree-1)*4)로 block 필드 내 offset 계산, 
		if(set_inode_onto_inode_table(fs, inode_num, &inodeBuffer))	// 수정한 아이노드 업데이트
		{
			return EXT2_ERROR;
		}
		process_meta_data_for_block_used(fs, available_block,0 );	// 해당 함수로 이동해서 제안을 읽어봐 주세요
		return EXT2_SUCCESS;
	}	
	else
	{
		if (checkFree==(12+1))											// 첫 번째 간접블록을 할당해야 할 경우
		{
			if(available_block = get_available_data_block(fs, inode_num)<0)					// 할당 가능한 데이터 블록 번호 읽어옴
			{
				printf("No empty block\n");
				return EXT2_ERROR;
			}
			memcpy(&(inodeBuffer[(12*4)+40]), &available_block, 4);		// 간접블록에 available_block 값 대입
			if(set_inode_onto_inode_table(fs, inode_num, &inodeBuffer))	// 수정한 아이노드 업데이트
			{
				return EXT2_ERROR;
			}
			process_meta_data_for_block_used(fs, available_block,0 );	// 해당 함수로 이동해서 제안을 읽어봐 주세요
		}
		else if (checkFree==(12+(blockSize/4)+1))						// 두 번째 간접블록을 할당해야 할 경우
		{
			if(available_block = get_available_data_block(fs, inode_num)<0)					// 할당 가능한 데이터 블록 번호 읽어옴
			{
				printf("No empty block\n");
				return EXT2_ERROR;
			}
			memcpy(&(inodeBuffer[(13*4)+40]), &available_block, 4);		// 간접블록에 available_block 값 대입
			if(set_inode_onto_inode_table(fs, inode_num, &inodeBuffer))	// 수정한 아이노드 업데이트
			{
				return EXT2_ERROR;
			}
			process_meta_data_for_block_used(fs, available_block,0 );	// 해당 함수로 이동해서 제안을 읽어봐 주세요
		}
		else if (checkFree==(12+(blockSize/4)+((blockSize/4)*(blockSize/4))+1))	// 세 번째 간접블록을 할당해야 할 경우
		{
			if(available_block = get_available_data_block(fs, inode_num)<0)					// 할당 가능한 데이터 블록 번호 읽어옴
			{
				printf("No empty block\n");
				return EXT2_ERROR;
			}
			memcpy(&(inodeBuffer[(14*4)+40]), &available_block, 4);		// 간접블록에 available_block 값 대입
			if(set_inode_onto_inode_table(fs, inode_num, &inodeBuffer))	// 수정한 아이노드 업데이트
			{
				return EXT2_ERROR;
			}
			process_meta_data_for_block_used(fs, available_block,0 );	// 해당 함수로 이동해서 제안을 읽어봐 주세요
		}

		reTurn = get_indirect_block_location_at_inode(fs, inodeBuffer, checkFree, &groupNumber, &groupOffset, &blockOffset);
		if(reTurn==EXT2_ERROR)
		{
			return EXT2_ERROR;
		}
		else if(reTurn==inode_data_empty)
		{
			while(1)
			{
				if(block_read(fs, groupNumber, groupOffset, blockBuffer))	
				{																	
					return EXT2_ERROR;
				}

				if(available_block = get_available_data_block(fs, inode_num)<0)						// 할당 가능한 데이터 블록 번호 읽어옴
				{
					printf("No empty block\n");
					return EXT2_ERROR;
				}

				memcpy( &(blockBuffer[(blockOffset-1)*4]), &available_block, 4);
				if(block_write(fs, groupNumber, groupOffset, blockBuffer))
				{
					return EXT2_ERROR;
				}
				process_meta_data_for_block_used(fs, available_block,0 );	// 해당 함수로 이동해서 제안을 읽어봐 주세요

				if(get_indirect_block_location_at_inode(fs, inodeBuffer, checkFree, &groupNumber, &groupOffset, &blockOffset)==EXT2_SUCCESS)
				{
					if(block_read(fs, groupNumber, groupOffset, blockBuffer))	
					{																	
						return EXT2_ERROR;
					}

					if(available_block = get_available_data_block(fs, inode_num)<0)						// 할당 가능한 데이터 블록 번호 읽어옴
					{
						printf("No empty block\n");
						return EXT2_ERROR;
					}

					memcpy( &(blockBuffer[(blockOffset-1)*4]), &available_block, 4);
					if(block_write(fs, groupNumber, groupOffset, blockBuffer))
					{
						return EXT2_ERROR;
					}
					break;
				}
			}
		}
		else
		{
			if(block_read(fs, groupNumber, groupOffset, blockBuffer))	
			{																	
				return EXT2_ERROR;
			}
			if(available_block = get_available_data_block(fs, inode_num)<0)						// 할당 가능한 데이터 블록 번호 읽어옴
			{
				printf("No empty block\n");					
				return EXT2_ERROR;
			}

			memcpy( &(blockBuffer[(blockOffset-1)*4]), &available_block, 4);
			if(block_write(fs, groupNumber, groupOffset, blockBuffer))
			{
				return EXT2_ERROR;
			}
			process_meta_data_for_block_used(fs, available_block,0 );	// 해당 함수로 이동해서 제안을 읽어봐 주세요
		}
		
	}

	return EXT2_SUCCESS;

	/*
	두 번째 인자로 받은 inode_num에 해당하는 아이노드를 찾아서(get_inode 이용하면 될 듯) 
	해당 아이노드의 block 필드의 비어있는 인덱스에 새로운 데이터 블록 할당
	seungmin */
	// 데이터블록 비트맵 업데이트 필요
}
int fill_super_block(EXT2_SUPER_BLOCK *sb, SECTOR numberOfSectors, UINT32 bytesPerSector) //슈퍼블록 포인터와 섹터 개수 섹터당 바이트 수를 받으면 슈퍼블록 구조체를 채워넣는다.
{
	ZeroMemory(sb, sizeof(EXT2_SUPER_BLOCK));

	sb->max_inode_count = NUMBER_OF_INODES;
	sb->block_count = numberOfSectors;
	sb->reserved_block_count = 0;
	sb->free_block_count = numberOfSectors - (17 * NUMBER_OF_GROUPS) - 1;
	sb->free_inode_count = NUMBER_OF_INODES - 10;
	sb->first_data_block = 1;
	sb->log_block_size = 0;
	sb->log_fragmentation_size = 0;
	sb->block_per_group = (numberOfSectors - 1) / NUMBER_OF_GROUPS;
	sb->fragmentation_per_group = 0;
	sb->inode_per_group = NUMBER_OF_INODES / NUMBER_OF_GROUPS;
	sb->magic_signature = 0xEF53;
	sb->errors = 0;
	sb->first_non_reserved_inode = 11;
	sb->inode_structure_size = 128;
	sb->block_group_number = 0;
	sb->first_data_block_each_group = 1 + 1 + 1 + 1 + 13;

	return EXT2_SUCCESS;
}
int fill_descriptor_block(EXT2_GROUP_DESCRIPTOR *gd, EXT2_SUPER_BLOCK *sb, SECTOR numberOfSectors, UINT32 bytesPerSector) //그룹 디스크립터 포인터와 슈퍼블록 포인터, 섹터 개수, 섹터당 바이트 수를 받으면 그룹디스크립터 구조체를 채워넣는다.
{
	ZeroMemory(gd, sizeof(EXT2_GROUP_DESCRIPTOR));

	gd->start_block_of_block_bitmap = 2;
	gd->start_block_of_inode_bitmap = 3;
	gd->start_block_of_inode_table = 4;
	gd->free_blocks_count = (UINT16)(sb->free_block_count / NUMBER_OF_GROUPS + sb->free_block_count % NUMBER_OF_GROUPS);
	gd->free_inodes_count = (UINT16)(((sb->free_inode_count) + 10) / NUMBER_OF_GROUPS - 10);
	gd->directories_count = 0;

	return EXT2_SUCCESS;
}
int create_root(DISK_OPERATIONS *disk, EXT2_SUPER_BLOCK *sb) //루트 디렉터리를 생성하는 함수
{
	BYTE sector[MAX_SECTOR_SIZE]; //
	SECTOR rootSector = 0;		  //
	EXT2_DIR_ENTRY *entry;		  //
	EXT2_GROUP_DESCRIPTOR *gd;	  //
	EXT2_SUPER_BLOCK *sb_read;	  //
	QWORD sector_num_per_group = (disk->numberOfSectors - 1) / NUMBER_OF_GROUPS;
	INODE *ip;
	const int BOOT_SECTOR_BASE = 1;
	int gi;

	// entry 초기화
	ZeroMemory(sector, MAX_SECTOR_SIZE);
	entry = (EXT2_DIR_ENTRY *)sector;

	memcpy(entry->name, VOLUME_LABLE, 11);
	entry->name_len = strlen(VOLUME_LABLE);
	entry->inode = 2; //루트 아이노드의 아이노드 번호는 2번
	entry++;
	entry->name[0] = DIR_ENTRY_NO_MORE;
	rootSector = 1 + sb->first_data_block_each_group;
	disk->write_sector(disk, rootSector, sector);

	// super block
	sb_read = (EXT2_SUPER_BLOCK *)sector;
	for (gi = 0; gi < NUMBER_OF_GROUPS; gi++)
	{
		disk->read_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE, sector);
		sb_read->free_block_count--;

		disk->write_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE, sector);
	}
	sb->free_block_count--;

	// group descriptor
	gd = (EXT2_GROUP_DESCRIPTOR *)sector;
	disk->read_sector(disk, BOOT_SECTOR_BASE + 1, sector);

	gd->free_blocks_count--;
	gd->directories_count = 1;

	for (gi = 0; gi < NUMBER_OF_GROUPS; gi++)
		disk->write_sector(disk, sector_num_per_group * gi + BOOT_SECTOR_BASE + 1, sector);

	// data block bitmap
	disk->read_sector(disk, BOOT_SECTOR_BASE + 2, sector);
	sector[2] |= 0x02;
	disk->write_sector(disk, BOOT_SECTOR_BASE + 2, sector);

	// ip 초기화
	ZeroMemory(sector, MAX_SECTOR_SIZE);
	ip = (INODE *)sector;
	ip++;
	ip->mode = 0x1FF | 0x4000;
	ip->size = 0;
	ip->blocks = 1;
	ip->block[0] = sb->first_data_block_each_group;
	disk->write_sector(disk, BOOT_SECTOR_BASE + 4, sector);

	return EXT2_SUCCESS;
}

void process_meta_data_for_block_used(EXT2_FILESYSTEM *fs, UINT32 inode_num)
{

	/*
	void process_meta_data_for_block_used(EXT2_FILESYSTEM * fs, UINT32 block_num, UNIT32 select)
	// block_num은 블록 번호, select는 해당 블록이 할당되었는지 해제되었는지를 의미
	{
		if(select==0)
		{
			block_num번 블록이 할당된 것에 대한 메타데이터 처리
		}
		else if(select==1)
		{
			block_num번 블록이 해제된 것에 대한 메타데이터 처리
		}
		else
		{
			printf("Error\n");
		}
	}
	함수 구현 방식에 대한 제안 -seungmin
	*/

  /*
	처리하다의 process
 	expand_block 등에서 데이터 블록을 할당, 해제했을 때 free_block_count, block_bitmap 등의 메타데이터를 알맞게 수정하는 함수
 	expand_block은 할당의 경우였고 해제의 경우에도 사용될 듯
 	다만 데이터 블록을 수정한 후 이 함수를 호출할 경우 이 함수에서 수정된 아이노드의 번호를가지고 수정내역을 다시 탐색 후 
	메타데이터를 수정해야 할 것 같은데 그럴 바에는 애초에 블록 할당이나 해제하는 함수에서 이 작업도 같이하는 편이 나을 것 같음
	seungmin */
	//inode번호로 아이노드 테이블에서 아이노드를 가져옴. 아이노드 데이터블럭에서 가장 마지막 블럭을 비트맵에 사용중이라고 표시
}

// Remove file (eunseo)
int ext2_remove(EXT2_NODE* file)
{
	INODE*	inodeBuffer;
	BYTE	sector[MAX_SECTOR_SIZE];	// 1024Byte
	int		result, i;
	UINT32	num, offset;				// num: 데이터블록 넘버, offset: 섹터 내에서 데이터블록 오프셋
	UINT16	mask;
	unsigned short fileTypeMask = 0xF000;

	inodeBuffer = (INODE *)malloc(sizeof(INODE));
	ZeroMemory(inodeBuffer, sizeof(INODE));
	result = get_inode(file->fs, file->entry.inode, inodeBuffer); // inode number에 대한 메타데이터를 inodeBuffer에 저장
	if (result == EXT2_ERROR)
		return EXT2_ERROR;

	if( (inodeBuffer->mode & fileTypeMask) && FILE_TYPE_DIR )  // 해당 엔트리가 디렉터리이면 에러
		return EXT2_ERROR;

	// 데이터블록 비트맵 수정
	for (i = 0; i < inodeBuffer->blocks; i++)
	{
		ZeroMemory(sector, MAX_SECTOR_SIZE);
		num = get_data_block_at_inode(file->fs, *inodeBuffer, i); // i번째 데이터블록 넘버

		data_read(file->fs, 0, file->fs->gd.start_block_of_block_bitmap, sector); // 데이터 블록 비트맵 sector 버퍼에 저장
		offset = (num+1) % 8; // 섹터 내의 offset 계산
		mask = ~(1 << offset); // 오프셋을 0으로 수정하기 위한 마스크
		sector[num/8] &= mask; // 비트맵 수정
		data_write(file->fs, 0, file->fs->gd.start_block_of_block_bitmap, sector); // 디스크에 수정된 비트맵 저장
	}

	// 아이노드 비트맵 수정
	ZeroMemory(sector, MAX_SECTOR_SIZE);
	data_read(file->fs, 0, file->fs->gd.start_block_of_inode_bitmap, sector); // 아이노드 비트맵 sector 버퍼에 저장
	offset = (file->entry.inode+1) % 8; // 섹터 내의 offset 계산
	mask = ~(1 << offset); // 오프셋을 0으로 수정하기 위한 마스크
	sector[file->entry.inode/8] &= mask; // 비트맵 수정
	data_write(file->fs, 0, file->fs->gd.start_block_of_inode_bitmap, sector); // 디스크에 수정된 비트맵 저장

	// 해제된 아이노드 데이터블럭 0으로 초기화
	for (i = 0; i < EXT2_N_BLOCKS; i++)
	{
		inodeBuffer->block[i] = 0;
	}

	// 삭제된 엔트리라고 저장
	file->entry.name[0] = DIR_ENTRY_FREE;
	set_entry(file->fs, &file->location, &file->entry);

	/*
	1. 아이노드에서 데이터 블록들을 확인해서 연결된 데이터 블록들에 대한 블록 비트맵에 들어가서 해당 블록을 할당가능 상태로 표시해 놓는다.
	2. 왠만하면 아이노드가 있는 같은 블록 그룹에 파일이 저장되지만, 만약 다른 블록 그룹에 있을 경우 해당 블록 그룹의 블록 비트맵에서 할당 가능 표시를 해놓는다.
	3. 아이노드에 표시되어 있는 데이터 블록을 모두 할당 해제 했다면, 아이노드를 나온다.
	4. 삭제할 파일의 아이노드 넘버를 알고 있으면, 아이노드가 있는 블록그룹의 아이노드 테이블에서 아이노드를 삭제하고, 아이노드 비트맵에서 사용가능이라고 표시해놓는다.
	5. 파일과 연결된 부모 디렉터리로 가서, 파일에 대한 디렉터리 엔트리의 이름을 DIR_ENTRY_FREE로 설정하고, 다른 연결을 해제한다.
	+) 동적할당한 것이 없기 때문에 해제하지는 않음
	*/
	return EXT2_SUCCESS;
}


// Read file (eunseo) - offset부터 length만큼 읽어서 buffer에 저장
int ext2_read(EXT2_NODE* file, unsigned long offset, unsigned long length, char* buffer)
{
	BYTE	sector[MAX_SECTOR_SIZE];					// 디스크에서 섹터 단위로 읽어오기 위한 버퍼
	DWORD	currentOffset, currentBlock, blockSeq = 0;	// currentOffset: 현재 읽고있는 offset 위치, currentBlock: 현재 읽고있는 데이터블록 번호, blockSeq: 몇번째 블록까지 읽었는지
	DWORD	blockNumber, sectorNumber, sectorOffset;	// blockNumber: 몇번째 블록인지, sectorNumber: 블록 내에서 몇번째 섹터인지, sectorOffset: 섹터 내에서 몇번째 offset인지
	DWORD	readEnd;
	DWORD	blockSize, blockOffset = 0;
	INODE	node;
	int		sectorsPerBlock = MAX_SECTOR_SIZE / MAX_BLOCK_SIZE;
	int i;

	get_inode(file->fs, file->entry.inode, &node); // 읽을 파일의 아이노드 메타데이터를 node에 저장
	currentBlock = node.block[0]; // 시작 블록 번호를 읽어옴

	blockSize = MAX_BLOCK_SIZE;
	blockOffset = blockSize; // 블럭 offset은 블럭 크기 단위로 증가
	i = 1;
	while (offset > blockOffset) // 읽고자 하는 위치에 맞게 currentBlock과 blockSeq 조정
	{
		currentBlock = get_data_block_at_inode(file->fs, node, ++i); // node의 i번째 데이터블록 번호
		blockOffset += blockSize; // blockOffset 증가
		blockSeq++; // 몇번째 블록까지 읽었는지 저장하는 변수 증가
	}

	currentOffset = offset; // 읽기 시작할 위치 offset
	readEnd = offset + length; // 읽고자 하는 마지막 위치

	while (currentOffset < readEnd) // 현재 offset이 읽고자 하는 위치보다 앞쪽인동안
	{
		DWORD	copyLength; // 복사할 데이터의 Byte단위 길이

		blockNumber = currentOffset / (file->fs->disk->bytesPerSector * sectorsPerBlock); // 현재 offset이 몇번째 블록인지 계산
		if (blockSeq != blockNumber) // 다음 블록으로 넘어갔다면
		{
			blockSeq++; // 몇번째 블록까지 읽었는지 저장하는 변수 증가
			++i;
			currentBlock = get_data_block_at_inode(file->fs, node, i); // 다음 블록으로 currentBlock을 변경
		}
		sectorNumber	= (currentOffset / file->fs->disk->bytesPerSector) % sectorsPerBlock; // 블록 내에서 몇번째 섹터인지 계산
		sectorOffset	= currentOffset % file->fs->disk->bytesPerSector; // 섹터 내에서 몇번째 offset인지 계산

		if ( data_read(file->fs, GET_INODE_GROUP(file->entry.inode), currentBlock, sector) ) // 계산한 위치의 데이터를 섹터단위로 읽음
			break;

		// 현재 읽어야 할 데이터가 마지막 데이터인지 판단. 마지막 데이터가 아니면 전자, 마지막 데이터이면 후자
		copyLength = MIN(file->fs->disk->bytesPerSector - sectorOffset, readEnd - currentOffset); // 다음 루프에서 버퍼로 복사할 크기

		memcpy(buffer, &sector[sectorOffset], copyLength); // 디스크에서 읽어온 데이터를 copyLength만큼 buffer에 복사

		buffer += copyLength; // 다음 데이터를 저장할 위치로 이동
		currentOffset += copyLength; // 다음 데이터를 읽기 위해 offset 조정
	}

	return currentOffset - offset; // 읽은 바이트 수 리턴
}


// Unmount file system
void ext2_umount(DISK_OPERATIONS* disk, SHELL_FS_OPERATIONS* fsOprs)
{
	return;		//함수 선언부, 인자 받는 부분 수정 필요시 수정해야 될 수도. 일단 fs_umount와 맞춰놓음
}

int ext2_df(EXT2_FILESYSTEM *fs, unsigned int * total, unsigned int * used)
{
	/*
	EXT2_FILESYSTEM의 EXT2_SUPER_BLOCK 필드에서 block_count와 block_count-free_block_count를 
	각각 total과 used에 저장(블록사이즈=섹터사이즈라 그냥 넣어도 될 듯)
	*/
	*total = fs->sb.block_count;
	*used = *total - fs->sb.free_block_count;

	return EXT2_SUCCESS;
}

// Remove directory
int ext2_rmdir(EXT2_NODE* dir)
{
	/*
	하위 엔트리 존재 여부 확인하여 에러 처리, 디렉토리인지 확인하여 에러처리(아이노드 등 확인해야 될 듯)
 	엔트리의 이름 DIR_ENTRY_FREE로 빈 엔트리로 표시
 	메타 데이터 수정(free_block_count 등)
	*/

	EXT2_NODE *_dir = dir;																 //EXT2_NODE 를 다른 포인터로 지정해 놓는다.
	INODE dir_inode;																	 //삭제하려는 디렉터리에 대한 아이노드 정보를 저장하는 변수
	UINT32 sector_num_per_block = MAX_BLOCK_SIZE / MAX_SECTOR_SIZE;						 //블럭당 섹터의 개수
	UINT32 inode_table_group_location;													 //inode table 저장된 블럭 번호 저장하는 변수.
	UINT32 inode_num_per_sector = MAX_SECTOR_SIZE / (_dir->fs->sb.inode_structure_size); //sector에 아이노드 구조체가 몇개 들어갈 수 있는지.
	UINT32 k = 0;																		 //그룹 번호 저장, 다용도
	EXT2_DIR_ENTRY *entry;																 //디렉터리 엔트리 저장하는 포인터
	BYTE sector[MAX_SECTOR_SIZE];														 //블럭 비트맵을 가져와서 저장할 공간.

	if (get_inode(_dir->fs, _dir->entry.inode, &dir_inode)) //삭제 하려는 디렉터리의아이노드 정보 읽어오기
	{
		if ((dir_inode.mode && 0x1111000000000000) == 0x4000) //mode 비트의 하위 9~11비트를 비교해서 디렉터리인지확인
		{
			if (dir_inode.links_count > 1) //디렉터리를 가르키는 하드링크수가 1개보다 많으면, 디렉터리를 가르키는 링크가 하나 더 있다는 의미이므로 지우려는 디렉터리 엔트리만 삭제
			{
				//아이노드 dir_inode.links_count 하나 줄이기, 디렉터리 엔트리 이름 DIR_ENTRY_FREE로 수정.
				//그룹디스크립터 directories_count 변수 수정.
				k = (_dir->entry.inode) / (_dir->fs->sb.inode_per_group);											//아이노드 번호를 그룹당 아이노드 개수로 나누어서 아이노드 속한 그룹 알아냄.
				inode_table_group_location = (((EXT2_GROUP_DESCRIPTOR *)_dir->fs) + k)->start_block_of_inode_table; //아이노드 테이블이 저장된 곳의 블럭 번호를 불러옴

				dir_inode.links_count--;
				set_inode_onto_inode_table(_dir->fs, _dir->entry.inode, &dir_inode); //아이노드 정보 바꿔서 넘겨줌.

				ZeroMemory(sector, MAX_SECTOR_SIZE);
				data_read(_dir->fs, _dir->location.group, _dir->location.block, sector); //sector에 디렉터리 엔트리가 들어있는 블럭 저장.
				Zeromeory(_dir->entry.name, _dir->entry.name_len);						 //이름을 바꾸기 전에 이름 영역 초기화.
				memcpy(_dir->entry.name, DIR_ENTRY_FREE, 1);							 //디렉터리 엔트리의 이름을 DIR_ENTRY_FREE로 수정
				memcpy(((EXT2_DIR_ENTRY *)sector + _dir->location.offset), &(_dir->entry), sizeof(EXT2_DIR_ENTRY));
				data_write(_dir->fs, _dir->location.group, _dir->location.block, sector); //디렉터리 엔트리 값을 바꾸고 저장.
				(((EXT2_GROUP_DESCRIPTOR *)_dir->fs) + k)->directories_count--;			  //그룹디스크립터의 디렉터리 수 감소
			}
			else //하드링크가 하나 연결되어 있는경우 폴더 완전 삭제를 의미
			{
				
				//1. 아이노드 할당해제 -> 다른 방법은 없고 그냥 비트맵을 사용가능으로 표시함으로써 해결
				//2. 아이노드 비트맵 수정 -> 아이노드가 속한 그룹 알아내서 그 그룹의 아이노드 비트맵 비트 사용가능 표시
				//3. 부모디렉터리에 저장되어 있는 디렉터리 엔트리 이름 DIR_ENTRY_FREE로 수정.
				//4. 그룹디스크립터 directories_count, free_inode_count 변수 수정.
				//5. 슈퍼블럭 free_inode_count 수정

				if (dir_inode.blocks == 1) //연결된 데이터 블럭이 있으면, 그 데이터 블럭을 탐색해서 데이터 블럭 내의 모든 디렉터리 엔트리가 사용중이지 않은 상태인지 검사. 하나라도 사용중인게 있으면 에러 발생.
				{
					ext2_read_dir(_dir, EXT2_NODE_ADD, entry); //adder 어디서 찾냐; entry에 잘 담겨 온다고 가정
					while (entry[k].name[0] != DIR_ENTRY_NO_MORE)	//엔트리 끝날때 까지 돈다. 루프 나온다는건 깔끔
					{
						if (entry[k].name[0] != DIR_ENTRY_FREE) //디렉터리 엔트리가 free가 아니라는건 뭐가 차있다는 것. 에러 발생.
						{
							return EXT2_ERROR;
						}
						k++;
					}
				}

				//디렉터리의 아이노드의 데이터 블럭이 0이라는건, 할당된 데이터 블럭이 없다는 뜻. 즉 디렉터리안에 아무것도 없으니 그냥 디렉터리 엔트리 지우면된다.
				//여기까지 온거면 연결된게 없다는 뜻 삭제진행
				inode_table_group_location = (_dir->entry.inode) / (_dir->fs->sb.inode_per_group); //이 디렉터리 엔트리의 아이노드 번호가 어느 그룹에 저장되어 있는지 저장.
				ZeroMemory(sector, MAX_SECTOR_SIZE);
				data_read(_dir->fs, inode_table_group_location, INODE_BITMAP, sector);	//sector에 아이노드가 들어있는 그룹의 아이노드 비트맵 저장.
				sector[_dir->entry.inode % _dir->fs->sb.inode_per_group] = 0;			//비트맵 값 변경.
				data_write(_dir->fs, inode_table_group_location, INODE_BITMAP, sector); //아이노드 비트맵 값을 바꾼 값으로 변경 후 다시 저장.

				ZeroMemory(sector, MAX_SECTOR_SIZE);
				data_read(_dir->fs, _dir->location.group, _dir->location.block, sector); //sector에 디렉터리 엔트리가 들어있는 블럭 저장.
				Zeromeory(_dir->entry.name, _dir->entry.name_len);						 //이름을 바꾸기 전에 이름 영역 초기화.
				memcpy(_dir->entry.name, DIR_ENTRY_FREE, 1);							 //디렉터리 엔트리의 이름을 DIR_ENTRY_FREE로 수정
				memcpy(((EXT2_DIR_ENTRY *)sector + _dir->location.offset), &(_dir->entry), sizeof(EXT2_DIR_ENTRY));
				data_write(_dir->fs, _dir->location.group, _dir->location.block, sector); //디렉터리 엔트리 값을 바꾸고 저장.

				(((EXT2_GROUP_DESCRIPTOR *)_dir->fs) + dir->location.group)->free_inodes_count++; //그룹디스크립터의 할당가능한 아이노드 수 증가
				(((EXT2_GROUP_DESCRIPTOR *)_dir->fs) + dir->location.group)->directories_count--; //그룹디스크립터의 사용중인 디렉터리 수 감소
				((EXT2_SUPER_BLOCK *)_dir->fs)->free_inode_count++;								  //슈퍼블럭의 비어있는 아이노드 수 증가.
			}
		}
		else
		{
			return EXT2_ERROR; //삭제하려는게 디렉터리가 아닌 경우
		}
	}
	else
	{
		return EXT2_ERROR; //삭제하려는 디렉터리의 아이노드 정보 읽어오기 실패
	}
}