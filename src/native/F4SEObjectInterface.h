// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
#pragma once
#include <cstdint>
#include <memory>
namespace F4SE { class SerializationInterface; }
namespace RE::BSScript { class Variable; }

// CommonLibF4RD deliberately exposes the F4SE Object interface while leaving
// these implementation classes opaque.  These declarations reproduce only
// the version-1 public vtable prefix shared by the reviewed official F4SE
// 0.6.23, 0.7.2 and 0.7.9 sources and the preserved 0.7.8 source. See
// .local/codex/docs/Phase4/CROSS_FAMILY_ABI.md. Never construct ObjectRegistry or
// DelayFunctorManager in plugin code; source agreement is not live validation.
namespace F4SE
{
	struct SerializationTag
	{};

	class DelayFunctor;

	class SerializableObject
	{
	public:
		virtual ~SerializableObject() = default;
		[[nodiscard]] virtual const char* ClassName() const = 0;
		[[nodiscard]] virtual std::uint32_t ClassVersion() const = 0;
		[[nodiscard]] virtual DelayFunctor* GetAsDelayFunctor() { return nullptr; }
		virtual bool Save(const SerializationInterface* a_intfc) = 0;
		virtual bool Load(const SerializationInterface* a_intfc, std::uint32_t a_version) = 0;
	};

	class DelayFunctor : public SerializableObject
	{
	public:
		~DelayFunctor() override = default;
		virtual bool Run(RE::BSScript::Variable& a_result) = 0;
		[[nodiscard]] DelayFunctor* GetAsDelayFunctor() override { return this; }
		virtual bool ShouldReschedule(std::int32_t& a_delayMilliseconds) = 0;
		virtual bool ShouldResumeStack(std::uint32_t& a_stackID) = 0;
	};

	class ObjectFactory
	{
	public:
		virtual ~ObjectFactory() = default;
		[[nodiscard]] virtual SerializableObject* Create() const = 0;
		virtual void Free(SerializableObject* a_object) const = 0;
		[[nodiscard]] virtual const char* ClassName() const = 0;
	};

	template <class T>
	class ConcreteObjectFactory final : public ObjectFactory
	{
	public:
		[[nodiscard]] SerializableObject* Create() const override { return new T(SerializationTag{}); }
		void Free(SerializableObject* a_object) const override { delete a_object; }
		[[nodiscard]] const char* ClassName() const override
		{
			T instance{ SerializationTag{} };
			return instance.ClassName();
		}
	};

	class ObjectRegistry
	{
	public:
		// Do not add a virtual destructor. RegisterFactory is vtable slot zero in
		// the F4SE version-1 ABI.
		virtual bool RegisterFactory(ObjectFactory* a_factory) = 0;
		[[nodiscard]] virtual const ObjectFactory* GetFactoryByName(const char* a_name) const = 0;

		template <class T>
		bool RegisterClass()
		{
			static_assert(sizeof(ConcreteObjectFactory<T>) == sizeof(void*));
			ConcreteObjectFactory<T> factory;
			return RegisterFactory(std::addressof(factory));
		}
	};

	// F4SE stores only a factory's vptr. The alias must be stateless, with its
	// lookup name encoded in its type. Created objects report the canonical
	// name, so future saves never write the alias. Payload loading is unchanged.
	template <class T, const char* ReadName>
	class ReadAliasObjectFactory final : public ObjectFactory
	{
	public:
		[[nodiscard]] SerializableObject* Create() const override { return new T(SerializationTag{}); }
		void Free(SerializableObject* a_object) const override { delete a_object; }
		[[nodiscard]] const char* ClassName() const override { return ReadName; }
	};

	template <class T, const char* ReadName>
	bool RegisterReadAlias(ObjectRegistry& a_registry)
	{
		static_assert(sizeof(ReadAliasObjectFactory<T, ReadName>) == sizeof(void*));
		ReadAliasObjectFactory<T, ReadName> factory;
		return a_registry.RegisterFactory(std::addressof(factory));
	}

	class DelayFunctorManager
	{
	public:
		// Do not add a virtual destructor. Enqueue is vtable slot zero in the
		// F4SE version-1 ABI and takes ownership of a_functor.
		virtual void Enqueue(DelayFunctor* a_functor, std::int32_t a_delayMilliseconds = 0) = 0;
	};

	static_assert(sizeof(SerializableObject) == sizeof(void*));
	static_assert(sizeof(DelayFunctor) == sizeof(void*));
	static_assert(sizeof(ObjectFactory) == sizeof(void*));
	static_assert(sizeof(ObjectRegistry) == sizeof(void*));
	static_assert(sizeof(DelayFunctorManager) == sizeof(void*));
}
