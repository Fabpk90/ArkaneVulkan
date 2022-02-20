#pragma once
class ISystem
{
public:
	ISystem(const ISystem& other)
	= default;

	ISystem(ISystem&& other) noexcept
	{
	}

	ISystem& operator=(const ISystem& other)
	{
		if (this == &other)
			return *this;
		return *this;
	}

	ISystem& operator=(ISystem&& other) noexcept
	{
		if (this == &other)
			return *this;
		return *this;
	}

	ISystem() = default;
	virtual ~ISystem() = default;

	virtual void Init() = 0;
	virtual void Start() = 0;
	virtual void Update() = 0;
};

